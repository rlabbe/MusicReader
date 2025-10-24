#include "document_load_manager.h"
#include "document.h"
#include <algorithm>
#include <thread>
#include <iostream>
#include <sstream>
#include <QtConcurrent>
#include "logger.h"



DocumentLoadManager *DocumentLoadManager::instance_ = nullptr;

DocumentLoadManager::DocumentLoadManager(int max_threads)
    : QObject(nullptr)
    , max_concurrent_jobs_(max_threads)
    , active_jobs_(0)
{
    logger::debug("DocumentLoadManager: Using " + std::to_string(max_concurrent_jobs_) + " threads");
    instance_ = this;
}

DocumentLoadManager::~DocumentLoadManager()
{
    stop_loading();

    // Wait for all active futures to actually finish
    for (auto &future : active_futures_) {
        if (!future.isFinished()) {
            future.waitForFinished();
        }
    }
    instance_ = nullptr;
}

void DocumentLoadManager::add_document(std::shared_ptr<Document> doc)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::filesystem::path filename = doc->filename();
    logger::debug("ADD_DOCUMENT: " + filename.stem().string());

    auto it = std::find_if(documents_.begin(), documents_.end(),
                          [&filename](const auto &d) { return d->filename() == filename; });

    if (it == documents_.end()) {
        documents_.push_back(doc);
        prioritize_page_internal(doc->filename());
    }

    if (!group_changes_) {
        populate_job_queue();
        submit_next_jobs();
    }
}

void DocumentLoadManager::end_group_changes()
{
    if (group_changes_) {
        group_changes_ = false;
        cancel_all_active_jobs_async();
        populate_job_queue();
        reorder_jobs();
        submit_next_jobs();
    }
}



void DocumentLoadManager::remove_document(const std::filesystem::path &filename)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    logger::debug("REMOVE_DOCUMENT: " + filename.stem().string());

    auto filename_str = filename.string();

    auto doc_it = std::find_if(documents_.begin(), documents_.end(),
                              [&filename_str](const auto &doc) {
        return doc->filename() == filename_str;
    });
    if (doc_it != documents_.end())
        (*doc_it)->kill_load();

    documents_.erase(std::remove_if(documents_.begin(), documents_.end(),
                                    [&filename_str](const auto &doc) {
        return doc->filename() == filename_str;
    }),
                    documents_.end());

    job_queue_.erase(std::remove_if(job_queue_.begin(), job_queue_.end(),
                                    [&filename](const PageJob &job) {
        return job.doc_path == filename;
    }),
                    job_queue_.end());

    document_priority_order_.erase(std::remove(document_priority_order_.begin(),
                                               document_priority_order_.end(),
                                               filename),
                                  document_priority_order_.end());

    cancel_all_active_jobs_async();
    submit_next_jobs();
}


void DocumentLoadManager::stop_loading()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    job_queue_.clear();
    cancel_all_active_jobs_async();
}


void DocumentLoadManager::populate_job_queue()
{
    job_queue_.clear();

    logger::debug("POPULATE_JOB_QUEUE:");

    for (const auto &doc : documents_) {
        if (!doc) continue;

        auto pending_pages = doc->get_pending_pages();
        if (pending_pages.size() == 0) 
            continue;

        auto doc_path = std::filesystem::path(doc->filename());

        std::ostringstream oss;
        oss << "  " << doc_path.stem().string() << " pending pages: ";
        for (int page_num : pending_pages) {
            oss << page_num << " ";
            job_queue_.emplace_back(doc, page_num, doc_path);
        }
        logger::debug(oss.str());
    }

    reorder_jobs();
}


void DocumentLoadManager::reorder_jobs()
{
    std::unordered_map<std::filesystem::path, std::vector<int>> doc_page_order;

    for (const auto &job : job_queue_) {
        doc_page_order[job.doc_path].push_back(job.page_num);
    }

    logger::debug("reorder_jobs call");
    std::sort(job_queue_.begin(), job_queue_.end(),
              [this, &doc_page_order](const PageJob &a, const PageJob &b) {
        auto a_priority = std::find(document_priority_order_.begin(),
                                  document_priority_order_.end(),
                                  a.doc_path);
        auto b_priority = std::find(document_priority_order_.begin(),
                                  document_priority_order_.end(),
                                  b.doc_path);

        if (a_priority != b_priority)
            return a_priority < b_priority;

        if (a.doc_path != b.doc_path)
            return false;

        const auto &page_order = doc_page_order[a.doc_path];
        auto a_pos = std::find(page_order.begin(), page_order.end(), a.page_num);
        auto b_pos = std::find(page_order.begin(), page_order.end(), b.page_num);

        return a_pos < b_pos;
    });

    std::ostringstream  oss;

    logger::debug("JOB_ORDER after reorder");
    if (job_queue_.size() < 100) {
        for (const auto &job : job_queue_)
            oss << job.doc_path.stem().string() << " " << job.page_num << " ";

    } else {
        for (size_t i = 0; i < 10 && i < job_queue_.size(); ++i)
            oss << job_queue_[i].doc_path.stem().string() << " " << job_queue_[i].page_num << " ";

        oss << "... (" << job_queue_.size() - 20 << " more jobs) ... ";
        for (size_t i = job_queue_.size() - 10; i < job_queue_.size(); ++i)
            oss << job_queue_[i].doc_path.stem().string() << " " << job_queue_[i].page_num << " ";
    }
    logger::debug(oss.str());

    logger::flush();
}


void DocumentLoadManager::submit_next_jobs()
{
    cleanup_finished_futures();

    while (active_jobs_ < max_concurrent_jobs_ && !job_queue_.empty()) {
        const auto &job = job_queue_.front();

        if (!job.document) {
            job_queue_.erase(job_queue_.begin());
            continue;
        }

        logger::debug("SUBMIT: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

        auto future = QtConcurrent::run([this, job]() {
            logger::debug("LOAD_START: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

            if (job.document)
                job.document->load_page(job.page_num);

            logger::debug("LOAD_DONE: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

            QMetaObject::invokeMethod(this, &DocumentLoadManager::on_job_completed, Qt::QueuedConnection);
        });

        active_futures_.push_back(future);
        active_jobs_++;
        job_queue_.erase(job_queue_.begin());
    }
}

void DocumentLoadManager::set_document_priority_order(const std::vector<std::filesystem::path> &ordered_docs)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (document_priority_order_ == ordered_docs) {
        logger::debug("no priority change");
        return;
    }

    if (group_changes_) {
        logger::debug("group changes active - skipping priority change");
        return;
    }

    std::ostringstream oss;
    oss << "PRIORITY_ORDER_CHANGED: ";
    for (const auto &doc : ordered_docs)
        oss << doc.stem().string() << " ";
    logger::debug(oss.str());

    // Keep the first document as highest priority
    std::filesystem::path highest_priority = ordered_docs[0];

    // Create new priority order: first document, then others sorted by pending pages
    std::vector<std::filesystem::path> new_order;
    new_order.push_back(highest_priority);

    // Collect remaining documents with their pending page counts
    std::vector<std::pair<std::filesystem::path, int>> remaining_docs;
    for (size_t i = 1; i < ordered_docs.size(); ++i) {
        const auto &path = ordered_docs[i];

        // Find the document and get its pending page count
        auto doc_it = std::find_if(documents_.begin(), documents_.end(),
                                  [&path](const auto &doc) {
            return doc && std::filesystem::path(doc->filename()) == path;
        });

        if (doc_it != documents_.end()) {
            int pending_count = static_cast<int>((*doc_it)->get_pending_pages().size());
            remaining_docs.emplace_back(path, pending_count);
        }
    }

    // Sort remaining by pending page count (ascending - fewer pages first)
    std::sort(remaining_docs.begin(), remaining_docs.end(),
              [](const auto &a, const auto &b) {
        return a.second < b.second;
    });

    // Add sorted documents to new order
    for (const auto &[path, count] : remaining_docs) {
        new_order.push_back(path);
    }

    document_priority_order_ = std::move(new_order);

    if (group_changes_)
        return;

    cancel_all_active_jobs_async();
    populate_job_queue();
    reorder_jobs();
    submit_next_jobs();
}

void DocumentLoadManager::prioritize_page(const Document &doc)
{
    if (group_changes_)
        return;

    prioritize_page(doc.filename());
}

void DocumentLoadManager::prioritize_page(const std::filesystem::path &filename)
{
    if (group_changes_)
        return;

    // If cancellation is in progress, just remember the filename and return immediately
    if (cancellation_in_progress_) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        pending_priority_doc_ = filename;
        has_pending_prioritization_ = true;
        logger::debug("PRIORITIZE_DEFERRED: " + filename.stem().string());
        return;
    }

    // If final processing is happening, block until it completes
    while (final_processing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    prioritize_page_internal(filename);
}

void DocumentLoadManager::prioritize_page_internal(const std::filesystem::path &filename)
{

    final_processing_ = true;
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    logger::debug("PRIORITIZE_PROCESSING: " + filename.stem().string());

    if (group_changes_) {
        final_processing_ = false;
        return;
    }

    if (document_priority_order_.empty()) {
        final_processing_ = false;
        return;
    }

    auto doc_it = std::find_if(documents_.begin(), documents_.end(),
                              [&filename](const auto &doc) {
        return std::filesystem::path(doc->filename()) == filename;
    });
    if (doc_it == documents_.end()) {
        final_processing_ = false;
        return;
    }

    TRACE_FUNCTION_MSG("filename: {}", filename.string());


    auto doc = *doc_it;
    auto priority_it = std::find(document_priority_order_.begin(), document_priority_order_.end(), filename);
    if (priority_it != document_priority_order_.begin()) {
        if (priority_it != document_priority_order_.end())
            document_priority_order_.erase(priority_it);

        document_priority_order_.insert(document_priority_order_.begin(), filename);
    }

    cancel_all_active_jobs_async();

    job_queue_.erase(std::remove_if(job_queue_.begin(), job_queue_.end(),
                                    [&filename](const PageJob &job) {
        return job.doc_path == filename;
    }), job_queue_.end());

    std::vector<int> pending = doc->get_pending_pages();
    if (!pending.empty()) {
        auto insert_pos = job_queue_.begin();
        for (int page : pending) {
            insert_pos = job_queue_.emplace(insert_pos, doc, page, filename);
            ++insert_pos;
        }
    }
    if (!job_queue_.empty()) {
        PageJob &job = job_queue_.front();
        logger::trace("first job in queue: file: {} page: {}", job.doc_path.string(), job.page_num);
    } else {
        logger::trace("job queue is empty");
    }
    submit_next_jobs();

    final_processing_ = false;
}


void DocumentLoadManager::on_job_completed()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    submit_next_jobs();
}


DocumentLoadManager *DocumentLoadManager::instance()
{
    return instance_;
}


void DocumentLoadManager::cancel_all_active_jobs_async()
{
    if (cancellation_in_progress_)
        return;

    TRACE_FUNCTION;

    cancellation_in_progress_ = true;

    logger::debug("CANCEL_ALL_ACTIVE_JOBS: canceling " + std::to_string(active_futures_.size()) + " jobs");

    for (auto &future : active_futures_) {
        if (!future.isFinished())
            future.cancel();
    }

    QTimer::singleShot(0, this, [this]() {
        cleanup_finished_futures();
        cancellation_in_progress_ = false;

        // Process any pending prioritization
        std::filesystem::path filename_to_process;
        bool should_process = false;

        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (has_pending_prioritization_) {
                filename_to_process = pending_priority_doc_;
                has_pending_prioritization_ = false;
                should_process = true;
                logger::debug("PRIORITIZE_PENDING_FOUND: " + filename_to_process.stem().string());
            }
        }

        if (should_process) {
            prioritize_page_internal(filename_to_process);
        }
    });
}

void DocumentLoadManager::cleanup_finished_futures()
{
    active_futures_.erase(std::remove_if(active_futures_.begin(), active_futures_.end(),
                                         [](const QFuture<void> &future) {
        return future.isFinished();
    }),
                         active_futures_.end());

    active_jobs_ = static_cast<int>(active_futures_.size());
}


DocumentLoadManager::LoadingSummary DocumentLoadManager::get_loading_summary() const
{
    // Quick check for empty state - minimize lock time
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (job_queue_.empty() && active_jobs_ == 0) {
            LoadingSummary summary;
            summary.total_active_jobs = 0;
            summary.total_queued_jobs = 0;
            return summary;
        }
    }

    // Copy data under lock - minimize lock time
    std::vector<PageJob> job_queue_copy;
    std::vector<std::shared_ptr<Document>> documents_copy;
    int active_jobs_copy;

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        job_queue_copy = job_queue_;
        documents_copy = documents_;
        active_jobs_copy = active_jobs_;
    }

    // Process data without lock
    LoadingSummary summary;
    summary.total_active_jobs = active_jobs_copy;
    summary.total_queued_jobs = static_cast<int>(job_queue_copy.size());

    // Group pending pages by document
    std::unordered_map<std::string, std::vector<int>> doc_pending_pages;

    // Add pages from job queue
    for (const auto &job : job_queue_copy)
        doc_pending_pages[job.doc_path.stem().string()].push_back(job.page_num);

    // Add pages from documents that might not be in queue yet
    for (const auto &doc : documents_copy) {
        if (!doc) continue;

        auto doc_name = std::filesystem::path(doc->filename()).stem().string();
        auto pending = doc->get_pending_pages();

        // Merge with existing pages from job queue
        auto &existing_pages = doc_pending_pages[doc_name];
        for (int page : pending)
            if (std::find(existing_pages.begin(), existing_pages.end(), page) == existing_pages.end())
                existing_pages.push_back(page);

        // Sort the pages
        std::sort(existing_pages.begin(), existing_pages.end());
    }

    // Convert to summary format
    for (const auto &[doc_name, pages] : doc_pending_pages)
        if (!pages.empty())
            summary.documents.push_back({ doc_name, pages });

    return summary;
}

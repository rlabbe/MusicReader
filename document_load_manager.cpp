#include "document_load_manager.h"
#include "document.h"
#include <algorithm>
#include <thread>
#include <iostream>
#include <sstream>
#include <QtConcurrent>

namespace {
std::mutex log_mutex;

#if defined NDEBUG
inline void thread_safe_log(const std::string &){}

#else
inline void thread_safe_log(const std::string &message)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << message << std::endl;
}
#endif
}

DocumentLoadManager *DocumentLoadManager::instance_ = nullptr;

DocumentLoadManager::DocumentLoadManager(int max_threads)
    : QObject(nullptr)
    , max_concurrent_jobs_(max_threads)
    , active_jobs_(0)
{
    thread_safe_log("DocumentLoadManager: Using " + std::to_string(max_concurrent_jobs_) + " threads");
    instance_ = this;
}

DocumentLoadManager::~DocumentLoadManager()
{
    stop_loading();
    if (instance_ == this)
        instance_ = nullptr;
}

void DocumentLoadManager::add_document(std::shared_ptr<Document> doc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::path filename = doc->filename();
    thread_safe_log("ADD_DOCUMENT: " + filename.stem().string());

    auto it = std::find_if(documents_.begin(), documents_.end(),
                          [&filename](const auto &d) { return d->filename() == filename; });

    if (it == documents_.end())
        documents_.push_back(doc);

    populate_job_queue();
    submit_next_jobs();
}

void DocumentLoadManager::remove_document(const std::filesystem::path &filename)
{
    std::lock_guard<std::mutex> lock(mutex_);

    thread_safe_log("REMOVE_DOCUMENT: " + filename.stem().string());

    auto filename_str = filename.string();

    // Kill loading for the document
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

    // Cancel any running jobs for this document
    cancel_all_active_jobs();
    submit_next_jobs();
}

void DocumentLoadManager::set_document_priority_order(const std::vector<std::filesystem::path> &ordered_docs)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    oss << "PRIORITY_ORDER_CHANGED: ";
    for (const auto &doc : ordered_docs)
        oss << doc.stem().string() << " ";
    thread_safe_log(oss.str());

    document_priority_order_ = ordered_docs;

    // Cancel all active jobs and restart with new priority
    cancel_all_active_jobs();
    reorder_jobs();
    submit_next_jobs();
}

void DocumentLoadManager::stop_loading()
{
    std::lock_guard<std::mutex> lock(mutex_);

    job_queue_.clear();
    cancel_all_active_jobs();
}

void DocumentLoadManager::populate_job_queue()
{
    job_queue_.clear();

    thread_safe_log("POPULATE_JOB_QUEUE:");

    for (const auto &doc : documents_) {
        if (!doc) continue;

        auto pending_pages = doc->get_pending_pages();
        auto doc_path = std::filesystem::path(doc->filename());

        std::ostringstream oss;
        oss << "  " << doc_path.stem().string() << " pending pages: ";
        for (int page_num : pending_pages) {
            oss << page_num << " ";
            job_queue_.emplace_back(doc, page_num, doc_path);
        }
        thread_safe_log(oss.str());
    }

    reorder_jobs();
}

void DocumentLoadManager::reorder_jobs()
{
    // Create a map to track original order within each document
    std::unordered_map<std::filesystem::path, std::vector<int>> doc_page_order;

    // Build the original page order for each document
    for (const auto &job : job_queue_) {
        doc_page_order[job.doc_path].push_back(job.page_num);
    }

    std::sort(job_queue_.begin(), job_queue_.end(),
              [this, &doc_page_order](const PageJob &a, const PageJob &b) {
        auto a_priority = std::find(document_priority_order_.begin(),
                                  document_priority_order_.end(),
                                  a.doc_path);
        auto b_priority = std::find(document_priority_order_.begin(),
                                  document_priority_order_.end(),
                                  b.doc_path);

        // First priority: document order
        if (a_priority != b_priority)
            return a_priority < b_priority;

        // Second priority: preserve original page order within same document
        const auto &page_order = doc_page_order[a.doc_path];
        auto a_pos = std::find(page_order.begin(), page_order.end(), a.page_num);
        auto b_pos = std::find(page_order.begin(), page_order.end(), b.page_num);

        return a_pos < b_pos;
    });

    std::ostringstream oss;
    oss << "JOB_ORDER after reorder: ";
    for (const auto &job : job_queue_)
        oss << job.doc_path.stem().string() << ":" << job.page_num << "\n";
    thread_safe_log(oss.str());
}

void DocumentLoadManager::submit_next_jobs()
{
    // Clean up completed futures
    active_futures_.erase(std::remove_if(active_futures_.begin(), active_futures_.end(),
                                         [](const QFuture<void> &future) {
        return future.isFinished();
    }),
                         active_futures_.end());

    active_jobs_ = static_cast<int>(active_futures_.size());

    while (active_jobs_ < max_concurrent_jobs_ && !job_queue_.empty()) {
        const auto &job = job_queue_.front();

        if (!job.document) {
            job_queue_.erase(job_queue_.begin());
            continue;
        }

        thread_safe_log("SUBMIT: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

        // Create future with cancellation support
        auto future = QtConcurrent::run([this, job]() {
            thread_safe_log("LOAD_START: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

            if (job.document)
                job.document->load_page(job.page_num);

            thread_safe_log("LOAD_DONE: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

            // Trigger next job submission
            QMetaObject::invokeMethod(this, &DocumentLoadManager::on_job_completed, Qt::QueuedConnection);
        });

        active_futures_.push_back(future);
        active_jobs_++;
        job_queue_.erase(job_queue_.begin());
    }
}


void DocumentLoadManager::prioritize_page(const std::filesystem::path &filename)
{
    std::lock_guard<std::mutex> lock(mutex_);
    thread_safe_log("PRIORITIZE_PAGE: " + filename.stem().string());
    // Find the document
    auto doc_it = std::find_if(documents_.begin(), documents_.end(),
                              [&filename](const auto &doc) {
        return std::filesystem::path(doc->filename()) == filename;
    });
    if (doc_it == documents_.end()) {
        thread_safe_log("PRIORITIZE_PAGE: Document not found: " + filename.stem().string());
        return;
    }
    auto doc = *doc_it;
    // Move document to front of priority order
    auto priority_it = std::find(document_priority_order_.begin(), document_priority_order_.end(), filename);
    if (priority_it != document_priority_order_.end()) {
        document_priority_order_.erase(priority_it);
    }
    document_priority_order_.insert(document_priority_order_.begin(), filename);
    // Cancel current jobs
    cancel_all_active_jobs();
    // Remove all jobs for this document
    job_queue_.erase(std::remove_if(job_queue_.begin(), job_queue_.end(),
                                    [&filename](const PageJob &job) {
        return job.doc_path == filename;
    }), job_queue_.end());
    // Get pending pages for this document (Document provides proper ordering)
    std::vector<int> pending = doc->get_pending_pages();
    if (!pending.empty()) {
        // Add jobs in the correct order
        for (int page : pending) {
            job_queue_.emplace_back(doc, page, filename);
        }
    }
    submit_next_jobs();
}



void DocumentLoadManager::on_job_completed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    submit_next_jobs();
}


DocumentLoadManager *DocumentLoadManager::instance()
{
    return instance_;
}

void DocumentLoadManager::cancel_all_active_jobs()
{
    thread_safe_log("CANCEL_ALL_ACTIVE_JOBS: canceling " + std::to_string(active_futures_.size()) + " jobs");

    for (auto &future : active_futures_) {
        if (!future.isFinished())
            future.cancel();
    }

    // Wait for all to finish or be cancelled
    for (auto &future : active_futures_) {
        future.waitForFinished();
    }

    active_futures_.clear();
    active_jobs_ = 0;
}
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
	if (instance_ == this)
		instance_ = nullptr;
}

void DocumentLoadManager::add_document(std::shared_ptr<Document> doc)
{
	std::lock_guard<std::mutex> lock(mutex_);
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
		cancel_all_active_jobs();
		populate_job_queue();
		reorder_jobs();
		submit_next_jobs();
	}
}



void DocumentLoadManager::remove_document(const std::filesystem::path &filename)
{
	std::lock_guard<std::mutex> lock(mutex_);

	logger::debug("REMOVE_DOCUMENT: " + filename.stem().string());

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

	if (document_priority_order_ == ordered_docs) {
		logger::debug("no priority change");
		return;
	}
	
	std::ostringstream oss;
	oss << "PRIORITY_ORDER_CHANGED: ";
	for (const auto &doc : ordered_docs)
		oss << doc.stem().string() << " ";
	logger::debug(oss.str());

	document_priority_order_ = ordered_docs;

	if (group_changes_) 
		return;
	
	// Cancel all active jobs and restart with new priority
	cancel_all_active_jobs();
	populate_job_queue();
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

	logger::debug("POPULATE_JOB_QUEUE:");

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

		// Only compare page order within the same document
		if (a.doc_path != b.doc_path)
			return false; // Equal priority, maintain stable sort

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
		// log first/last 10 jobs
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

		logger::debug("SUBMIT: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

		// Create future with cancellation support
		auto future = QtConcurrent::run([this, job]() {
			logger::debug("LOAD_START: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

			if (job.document)
				job.document->load_page(job.page_num);

			logger::debug("LOAD_DONE: " + job.doc_path.stem().string() + " page " + std::to_string(job.page_num));

			// Trigger next job submission
			QMetaObject::invokeMethod(this, &DocumentLoadManager::on_job_completed, Qt::QueuedConnection);
		});

		active_futures_.push_back(future);
		active_jobs_++;
		job_queue_.erase(job_queue_.begin());
	}
}

void DocumentLoadManager::prioritize_page(const Document &doc)
{
	prioritize_page(doc.filename());
}


void DocumentLoadManager::prioritize_page(const std::filesystem::path &filename)
{
	std::lock_guard<std::mutex> lock(mutex_);
	prioritize_page_internal(filename);
}

// no lock, calling internally from function that already has the lock
void DocumentLoadManager::prioritize_page_internal(const std::filesystem::path &filename)
{
	if (group_changes_) 
		// safe to ignore, so long as at the end you f
		return;

	logger::debug("PRIORITIZE_PAGE: " + filename.stem().string());
	// Find the document
	auto doc_it = std::find_if(documents_.begin(), documents_.end(),
							  [&filename](const auto &doc) {
		return std::filesystem::path(doc->filename()) == filename;
	});
	if (doc_it == documents_.end()) {
		logger::debug("PRIORITIZE_PAGE: Document not found: " + filename.stem().string());
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
	logger::debug("CANCEL_ALL_ACTIVE_JOBS: canceling " + std::to_string(active_futures_.size()) + " jobs");

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
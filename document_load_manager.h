#pragma once

#include <memory>
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <set>
#include <QObject>
#include <QFuture>
#include <QPromise>
#include <QTimer>


/*
  DocumentLoadManager - Non-blocking PDF page loading with prioritization

  OVERVIEW:
  Manages background loading of PDF pages across multiple documents using a thread pool.
  Provides non-blocking prioritization to keep UI responsive during page navigation.

  KEY BEHAVIORS:
  1. Thread Pool: Uses QtConcurrent to load pages in background threads
  2. Priority Ordering: Documents can be reordered to prioritize loading
  3. Non-blocking Prioritization: Page up/down navigation returns immediately
  4. Smart Queuing: Only loads pending (empty) pages, skips already loaded ones
  5. Cancellation: Can cancel active jobs when priorities change

  PRIORITIZATION LOGIC:
  - prioritize_page() called on page navigation (up/down arrow keys)
  - If cancellation in progress: remembers filename, returns immediately
  - If final processing happening: blocks briefly (about 1ms) to avoid races
  - Otherwise: processes immediately with full priority reordering
  - Multiple rapid calls during cancellation -> only latest filename processed

  THREAD SAFETY:
  - Uses recursive_mutex to allow reentrant locking (e.g., add_document -> prioritize)
  - Atomic flags track cancellation and processing states
  - QtConcurrent futures provide cancellation support

  PERFORMANCE NOTES:
  - Expensive operation: waiting for active jobs to cancel (non-blocking)
  - Cheap operation: priority reordering and job queue manipulation (can block briefly)
  - Load order within document: current page, next page, previous page, then outward
 */

class Document;
class DocumentLoadManager;

struct PageJob {
    std::shared_ptr<Document> document;
    int page_num;
    std::filesystem::path doc_path;

    PageJob(std::shared_ptr<Document> doc, int page, std::filesystem::path path)
        : document(std::move(doc))
        , page_num(page)
        , doc_path(std::move(path))
    {
    }
};


class DocumentLoadManager : public QObject {
    Q_OBJECT

public:
    DocumentLoadManager(int max_threads = std::thread::hardware_concurrency());
    ~DocumentLoadManager();

    void add_document(std::shared_ptr<Document> doc);
    void remove_document(const std::filesystem::path& filename);
    void set_document_priority_order(const std::vector<std::filesystem::path>& ordered_docs);
    void prioritize_page(const std::filesystem::path& filename);
    void prioritize_page(const Document& doc);

    void start_group_changes() { group_changes_ = true; }
    void end_group_changes();

    void stop_loading();

    static DocumentLoadManager* instance();


    // For debugging and monitoring
    struct DocumentLoadInfo {
        std::string name;
        std::vector<int> pending_pages;
    };

    struct LoadingSummary {
        std::vector<DocumentLoadInfo> documents;
        int total_active_jobs = 0;
        int total_queued_jobs = 0;
    };

    LoadingSummary get_loading_summary() const;

private slots:
    void on_job_completed();

private:
    void prioritize_page_internal(const std::filesystem::path& filename);

    void populate_job_queue();
    void reorder_jobs();
    void submit_next_jobs();
    void cancel_all_active_jobs_async();
    void cleanup_finished_futures();

    // Memory-aware rate limiting
    int calculate_max_concurrent_jobs() const;
    struct MemoryStats {
        size_t available_memory_mb = 0;
        size_t total_memory_mb = 0;
        int memory_pressure_percent = 0; // 0-100, higher = more pressure
    };
    MemoryStats get_memory_stats() const;

    std::vector<PageJob> job_queue_;
    std::vector<std::filesystem::path> document_priority_order_;
    std::vector<std::shared_ptr<Document>> documents_;
    std::vector<QFuture<void>> active_futures_;
    std::set<std::pair<std::filesystem::path, int>> active_jobs_set_;

    mutable std::recursive_mutex mutex_;
    int max_concurrent_jobs_;
    int active_jobs_;
    std::atomic<bool> group_changes_ = false;

    // Non-blocking prioritization support
    std::atomic<bool> cancellation_in_progress_ {false};
    std::atomic<bool> final_processing_ {false};
    std::filesystem::path pending_priority_doc_;
    bool has_pending_prioritization_ = false;

    // Memory pressure tracking
    mutable int last_memory_pressure_level_ = 0; // 0=none, 1=light, 2=medium, 3=high, 4=critical

    static DocumentLoadManager* instance_;
};


template<typename T> class GroupChangesGuard {
public:
    GroupChangesGuard(T& manager)
        : manager_(manager)
    {
        manager_.start_group_changes();
    }
    ~GroupChangesGuard() { manager_.end_group_changes(); }

private:
    T& manager_;
};


class DocumentLoadManagerGuard : public GroupChangesGuard<DocumentLoadManager> {
public:
    DocumentLoadManagerGuard()
        : GroupChangesGuard<DocumentLoadManager>(*DocumentLoadManager::instance())
    {
    }

    ~DocumentLoadManagerGuard() = default;
};

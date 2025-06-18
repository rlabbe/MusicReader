#pragma once

#include <memory>
#include <filesystem>
#include <vector>
#include <mutex>
#include <QObject>
#include <QFuture>
#include <QPromise>

class Document;
class DocumentLoadManager;

struct PageJob {
    std::shared_ptr<Document> document;
    int page_num;
    std::filesystem::path doc_path;

    PageJob(std::shared_ptr<Document> doc, int page, std::filesystem::path path)
        : document(std::move(doc)), page_num(page), doc_path(std::move(path))
    {
    }
};

class DocumentLoadManager : public QObject {
    Q_OBJECT

public:
    DocumentLoadManager(int max_threads = std::thread::hardware_concurrency());
    ~DocumentLoadManager();

    void add_document(std::shared_ptr<Document> doc);
    void remove_document(const std::filesystem::path &filename);
    void set_document_priority_order(const std::vector<std::filesystem::path> &ordered_docs);
    void prioritize_page(const std::filesystem::path &filename);

    void stop_loading();

    // Singleton access for documents to call
    static DocumentLoadManager *instance();

private slots:
    void on_job_completed();

private:
    void populate_job_queue();
    void reorder_jobs();
    void submit_next_jobs();
    void cancel_all_active_jobs();

    std::vector<PageJob> job_queue_;
    std::vector<std::filesystem::path> document_priority_order_;
    std::vector<std::shared_ptr<Document>> documents_;
    std::vector<QFuture<void>> active_futures_;

    std::mutex mutex_;
    int max_concurrent_jobs_;
    int active_jobs_;

    static DocumentLoadManager *instance_;
};
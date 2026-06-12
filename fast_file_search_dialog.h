#pragma once

#include <QtWidgets>
#include <mutex>
#include <condition_variable>
#include <filesystem>

class ConfigFile;
class DirectoryWatcher;

class SortableTableWidgetItem : public QTableWidgetItem {
public:
    // Numeric-sorted column (date, size).
    SortableTableWidgetItem(qint64 sort_value, const QString& text, bool favorite);
    // Text-sorted column (name, favorite star).
    SortableTableWidgetItem(const QString& text, bool favorite);

    bool operator<(const QTableWidgetItem& other) const override;
    bool is_favorite() const { return favorite_; }

private:
    bool favorite_ = false;
    bool has_sort_value_ = false;
    qint64 sort_value_ = 0;
};

class UpdateSignal : public QObject {
    Q_OBJECT

public:
    explicit UpdateSignal(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    static UpdateSignal& instance()
    {
        static UpdateSignal instance_;
        return instance_;
    }

signals:
    void filesUpdated();
};

class FastFileSearchDialog : public QDialog {
    Q_OBJECT

public:
    // DO NOT constrcut until initialize_data has been called
    FastFileSearchDialog(QWidget* parent, const ConfigFile& config, const QRect& size);

    static void initialize_data(const std::filesystem::path& directory);

    static void directory_changed(const std::filesystem::path& new_search_path, FastFileSearchDialog* self = nullptr);
    static void class_file_changed();

    void show_dialog();

    std::filesystem::path path() const { return path_; }
    std::pair<std::vector<std::filesystem::path>, std::filesystem::path> selected_files() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
    void reject();

private slots:
    void instance_update_files();
    void update_files();
    void on_search();
    void on_select_directory();
    void on_open_file_dialog();
    void show_context_menu(const QPoint& pos);
    void browse_to_directory();
    void on_item_double_click(QTableWidgetItem* item);
    void on_favorite_clicked(int row, int column);
    void accept();
    void show_help();

private:
    void set_title();
    void init_ui(const QRect& size);
    void display_files(const QStringList& file_paths, bool resize = false);
    void size_button(QPushButton* button);
    static QStringList find_files(const std::filesystem::path& path, QString& extension);
    void delete_selected_files();
    void restore_deleted_files();

    bool search_term_entered() const;

    static inline std::filesystem::path path_;
    static inline QStringList files_;
    static inline std::mutex files_mutex_;
    static inline std::condition_variable files_cv_;
    static inline bool files_ready_ = false;
    static inline std::vector<std::filesystem::path> recently_deleted_;

    static inline DirectoryWatcher* watcher_ = nullptr;
    static inline QString file_ending_ = "pdf";

    QVBoxLayout* layout_;
    QHBoxLayout* top_layout_;
    QLabel* label_;
    QLineEdit* search_field_;
    QTableWidget* file_table_;
    QStringList selected_items_;
    QString open_path_;


    static inline FastFileSearchDialog* instance_ = nullptr;
    const ConfigFile& config_;
    bool positioned_ = false;
};
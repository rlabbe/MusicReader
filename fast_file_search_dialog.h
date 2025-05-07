#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFileInfo>
#include <QHeaderView>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QKeyEvent>
#include <QApplication>
#include <QTimer>
#include <QStringList>
#include <mutex>
#include <condition_variable>
#include <QFileSystemWatcher>
#include "directory_watcher.h"



class SortableTableWidgetItem : public QTableWidgetItem {
public:
    explicit SortableTableWidgetItem(int sort_value, const QString &text);
    bool operator<(const QTableWidgetItem &other) const override;

private:
    int sort_value_;
};

class UpdateSignal : public QObject {
    Q_OBJECT

public:
    explicit UpdateSignal(QObject *parent = nullptr) : QObject(parent) {}
    static UpdateSignal &instance()
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
    FastFileSearchDialog(QWidget *parent, const std::string &directory_path, const QRect &size);

    static void initialize_data(const std::string &directory);

    static void directory_changed(const std::string &new_search_path, FastFileSearchDialog *self = nullptr);
    static void class_file_changed();

    void show_dialog();

    std::string path() const { return path_.toStdString(); }
    std::pair<std::vector<std::string>, std::string> selected_files() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void reject();

private slots:
    void instance_update_files();
    void update_files();
    void on_search();
    void on_select_directory();
    void on_open_file_dialog();
    void show_context_menu(const QPoint &pos);
    void browse_to_directory();
    void on_item_double_click(QTableWidgetItem *item);
    void accept();
    void show_help();

private:
    void set_title();
    void init_ui(const QRect &size);
    void display_files(const QStringList &file_paths, bool resize=false);
    void size_button(QPushButton *button);
    static QStringList find_files(const QString& path, QString& extension);

    void _open_help();

    static inline QString path_;
    static inline QStringList files_;
    static inline std::mutex files_mutex_;
    static inline std::condition_variable files_cv_;
    static inline bool files_ready_ = false;

    static inline DirectoryWatcher *watcher_ = nullptr;
    static inline QString file_ending_ = "pdf";

    QVBoxLayout *layout_;
    QHBoxLayout *top_layout_;
    QLabel *label_;
    QLineEdit *search_field_;
    QTableWidget *file_table_;
    QStringList selected_items_;
    QString open_path_;

    static inline FastFileSearchDialog *instance_ = nullptr;
};

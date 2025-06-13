#pragma once

#include <QDialog>
#include <memory>
#include "musicreader.h"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QWebEngineView;
class QWebEngineDownloadRequest;
class IMSLPClient;
struct FileInfo;

class IMSLPSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit IMSLPSearchDialog(MusicReader *parent);
    ~IMSLPSearchDialog();

private:
    void setup_ui();
    void setup_web_engine();
    void clear_results();
    void add_result_to_list(const FileInfo &pdf);
    void download_thumbnail(QListWidgetItem *item, const QString &thumb_url);
    void update_view_mode();
    int get_icon_size() const;
    void download_and_open_pdf(QListWidgetItem *item);
    QString get_temp_file_path(const QString &filename) const;

private slots:
    void on_search_clicked();
    void on_thumbnail_downloaded();
    void on_list_view_clicked();
    void on_grid_view_clicked();
    void on_small_icon_clicked();
    void on_large_icon_clicked();
    void on_item_double_clicked(QListWidgetItem *item);
    void on_web_engine_download_requested(QWebEngineDownloadRequest *download);
    void on_web_engine_load_finished(bool success);

private:
    enum class ViewMode { List, Grid };
    enum class IconSize { Small, Large };

    // UI elements
    QLineEdit *search_edit_;
    QPushButton *search_button_;
    QLabel *status_label_;
    QProgressBar *progress_bar_;
    QListWidget *results_list_;

    // View control buttons
    QPushButton *list_view_button_;
    QPushButton *grid_view_button_;
    QPushButton *small_icon_button_;
    QPushButton *large_icon_button_;

    // Current state
    ViewMode view_mode_ = ViewMode::Grid;
    IconSize icon_size_ = IconSize::Small;
    std::vector<FileInfo> current_results_;

    // Network and client
    std::unique_ptr<IMSLPClient> client_;
    QNetworkAccessManager *network_manager_;
    QWebEngineView *web_view_;

    // Current download info
    QString current_download_path_;
    QString current_download_filename_;
};
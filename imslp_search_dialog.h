#pragma once

#include <QtWidgets>
#include <memory>
#include "musicreader.h"

class IMSLPClient;
struct FileInfo;
class QWebEngineView;
class QNetworkAccessManager;
class QWebEngineDownloadRequest;

class IMSLPSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit IMSLPSearchDialog(MusicReader* parent);
    ~IMSLPSearchDialog();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void reject() override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void setup_ui();
    void setup_web_engine();
    void clear_results();
    void add_result_to_list(const FileInfo& pdf);
    void download_thumbnail(QListWidgetItem* item, const QString& thumb_url);
    void update_view_mode();
    int get_icon_size() const;
    void download_and_open_pdf(QListWidgetItem* item);
    QString get_temp_file_path(const QString& filename) const;
    QString save_file_to_permanent_location(const QString& temp_path, const QString& filename,
                                            MusicReader* main_window);

private slots:
    void on_search_clicked();
    void on_thumbnail_downloaded();
    void on_list_view_clicked();
    void on_grid_view_clicked();
    void on_small_icon_clicked();
    void on_large_icon_clicked();
    void on_item_double_clicked(QListWidgetItem* item);
    void on_web_engine_download_requested(QWebEngineDownloadRequest* download);
    void on_web_engine_load_finished(bool success);
    void show_hover_popup(QListWidgetItem* item);
    void hide_hover_popup();

private:
    enum class ViewMode {
        List,
        Grid
    };
    enum class IconSize {
        Small,
        Large
    };

    QWidget* web_main_widget_; // Main widget for IMSLP web content
    // UI elements
    QLineEdit* search_edit_;
    QPushButton* search_button_;
    QLabel* status_label_;
    QProgressBar* progress_bar_;
    QListWidget* results_list_;

    // View control buttons
    QPushButton* list_view_button_;
    QPushButton* grid_view_button_;
    QPushButton* small_icon_button_;
    QPushButton* large_icon_button_;

    // Current state
    ViewMode view_mode_ = ViewMode::List;
    IconSize icon_size_ = IconSize::Large;
    std::vector<FileInfo> current_results_;

    // Network and client
    std::unique_ptr<IMSLPClient> client_;
    QNetworkAccessManager* network_manager_;
    QWebEngineView* web_view_;

    // Current download info
    QString current_download_path_;
    QString current_download_filename_;

    // File prefixes to strip
    std::vector<std::string> file_prefixes_ = {"PMLP", "IMSLP"};

    QString last_save_directory_;

    // Hover functionality
    QLabel* hover_popup_;

    int large_icon_size_ = 300;
    int small_icon_size_ = 150;
};
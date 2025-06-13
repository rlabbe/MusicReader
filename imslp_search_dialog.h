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
    explicit IMSLPSearchDialog(QWidget *parent = nullptr);
    ~IMSLPSearchDialog();

private:
    void setupUI();
    void setupWebEngine();
    void clearResults();
    void addResultToList(const FileInfo &pdf);
    void downloadThumbnail(QListWidgetItem *item, const QString &thumbUrl);
    void updateViewMode();
    int getIconSize() const;
    void downloadAndOpenPdf(QListWidgetItem *item);
    QString getTempFilePath(const QString &filename) const;

private slots:
    void onSearchClicked();
    void onThumbnailDownloaded();
    void onListViewClicked();
    void onGridViewClicked();
    void onSmallIconClicked();
    void onLargeIconClicked();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onWebEngineDownloadRequested(QWebEngineDownloadRequest *download);
    void onWebEngineLoadFinished(bool success);

private:
    enum class ViewMode { List, Grid };
    enum class IconSize { Small, Large };

    // UI elements
    QLineEdit *m_searchEdit;
    QPushButton *m_searchButton;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QListWidget *m_resultsList;

    // View control buttons
    QPushButton *m_listViewButton;
    QPushButton *m_gridViewButton;
    QPushButton *m_smallIconButton;
    QPushButton *m_largeIconButton;

    // Current state
    ViewMode m_viewMode = ViewMode::Grid;
    IconSize m_iconSize = IconSize::Small;
    std::vector<FileInfo> m_currentResults;

    // Network and client
    std::unique_ptr<IMSLPClient> m_client;
    QNetworkAccessManager *m_networkManager;
    QWebEngineView *m_webView;

    // Current download info
    QString m_currentDownloadPath;
    QString m_currentDownloadFilename;
};
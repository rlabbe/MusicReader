#pragma once

#include <QDialog>
#include <memory>
#include <vector>
#include "imslp_client.h"

class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;

class IMSLPSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit IMSLPSearchDialog(QWidget *parent = nullptr);
    ~IMSLPSearchDialog();

private slots:
    void onSearchClicked();
    void onThumbnailDownloaded();
    void onListViewClicked();
    void onGridViewClicked();
    void onSmallIconClicked();
    void onLargeIconClicked();

private:
    void setupUI();
    void clearResults();
    void addResultToList(const FileInfo &pdf);
    void downloadThumbnail(QListWidgetItem *item, const QString &thumbUrl);
    void updateViewMode();
    int getIconSize() const;

    QLineEdit *m_searchEdit;
    QPushButton *m_searchButton;
    QListWidget *m_resultsList;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QNetworkAccessManager *m_networkManager;

    std::unique_ptr<IMSLPClient> m_client;
    std::vector<FileInfo> m_currentResults;

    // View controls
    QPushButton *m_listViewButton;
    QPushButton *m_gridViewButton;
    QPushButton *m_smallIconButton;
    QPushButton *m_largeIconButton;

    enum class ViewMode { List, Grid };
    enum class IconSize { Small, Large };
    ViewMode m_viewMode = ViewMode::Grid;
    IconSize m_iconSize = IconSize::Small;
};
#pragma once

#include <QDialog>
#include <memory>
#include <vector>
#include "imslp_client.h"

// Forward declarations
class QLineEdit;
class QPushButton;
class QListWidget;
class QLabel;
class QProgressBar;
class QNetworkAccessManager;
class QListWidgetItem;

class IMSLPSearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit IMSLPSearchDialog(QWidget *parent = nullptr);
    ~IMSLPSearchDialog();

private slots:
    void onSearchClicked();
    void onThumbnailDownloaded();

private:
    void setupUI();
    void clearResults();
    void addResultToList(const FileInfo &pdf);
    void downloadThumbnail(QListWidgetItem *item, const QString &thumbUrl);

    QLineEdit *m_searchEdit;
    QPushButton *m_searchButton;
    QListWidget *m_resultsList;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;

    std::unique_ptr<IMSLPClient> m_client;
    QNetworkAccessManager *m_networkManager;
    std::vector<FileInfo> m_currentResults;
};
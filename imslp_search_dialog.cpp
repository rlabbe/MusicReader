#include "imslp_search_dialog.h"
#include <QApplication>
#include <QMessageBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QUrl>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>

IMSLPSearchDialog::IMSLPSearchDialog(QWidget *parent)
    : QDialog(parent)
    , m_client(std::make_unique<IMSLPClient>())
    , m_networkManager(new QNetworkAccessManager(this))
{
    setupUI();
    setWindowTitle("IMSLP Search");
    setMinimumSize(600, 400);
    resize(800, 600);
}

IMSLPSearchDialog::~IMSLPSearchDialog() = default;

void IMSLPSearchDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Search section
    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Enter composer, work title, or BWV number...");
    searchLayout->addWidget(m_searchEdit);

    m_searchButton = new QPushButton("Search");
    searchLayout->addWidget(m_searchButton);

    mainLayout->addLayout(searchLayout);

    // Status section
    m_statusLabel = new QLabel("Enter search terms and click Search");
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // Results section
    mainLayout->addWidget(new QLabel("Results:"));

    m_resultsList = new QListWidget();
    m_resultsList->setIconSize(QSize(100, 100));
    mainLayout->addWidget(m_resultsList);

    // Connect signals
    connect(m_searchButton, &QPushButton::clicked, this, &IMSLPSearchDialog::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &IMSLPSearchDialog::onSearchClicked);
}

void IMSLPSearchDialog::onSearchClicked()
{
    QString searchText = m_searchEdit->text().trimmed();
    if (searchText.isEmpty()) {
        QMessageBox::warning(this, "Search", "Please enter search terms");
        return;
    }

    clearResults();
    m_searchButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0); // Indeterminate progress
    m_statusLabel->setText("Searching...");

    // Run search in background thread
    auto *watcher = new QFutureWatcher<std::vector<FileInfo>>(this);
    connect(watcher, &QFutureWatcher<std::vector<FileInfo>>::finished, [this, watcher]() {
        try {
            auto results = watcher->result();
            m_currentResults = std::move(results);

            m_progressBar->setVisible(false);
            m_searchButton->setEnabled(true);

            if (m_currentResults.empty()) {
                m_statusLabel->setText("No results found");
            } else {
                m_statusLabel->setText(QString("Found %1 PDF(s)").arg(m_currentResults.size()));

                // Add results to list
                for (const auto &pdf : m_currentResults) {
                    addResultToList(pdf);
                }
            }
        } catch (const std::exception &e) {
            m_progressBar->setVisible(false);
            m_searchButton->setEnabled(true);
            m_statusLabel->setText("Search failed");
            QMessageBox::critical(this, "Search Error", QString("Search failed: %1").arg(e.what()));
        }
        watcher->deleteLater();
    });

    QFuture<std::vector<FileInfo>> future = QtConcurrent::run([this, searchText]() {
        return m_client->get_work_pdfs(searchText.toStdString());
    });

    watcher->setFuture(future);
}

void IMSLPSearchDialog::clearResults()
{
    m_resultsList->clear();
    m_currentResults.clear();
}

void IMSLPSearchDialog::addResultToList(const FileInfo &pdf)
{
    // Extract just the filename without "File:" prefix for display
    QString displayName = QString::fromStdString(pdf.filename);
    if (displayName.startsWith("File:")) {
        displayName = displayName.mid(5); // Remove "File:" prefix
    }

    // Create list item
    auto *item = new QListWidgetItem(displayName);
    item->setData(Qt::UserRole, QString::fromStdString(pdf.url)); // Store full PDF URL
    item->setData(Qt::UserRole + 1, QString::fromStdString(pdf.thumb_url)); // Store thumbnail URL

    // Set a default icon while thumbnail loads
    QPixmap defaultPixmap(100, 100);
    defaultPixmap.fill(Qt::lightGray);
    item->setIcon(QIcon(defaultPixmap));

    m_resultsList->addItem(item);

    // Download thumbnail if available
    if (!pdf.thumb_url.empty()) {
        downloadThumbnail(item, QString::fromStdString(pdf.thumb_url));
    }
}

void IMSLPSearchDialog::downloadThumbnail(QListWidgetItem *item, const QString &thumbUrl)
{
    QString url = thumbUrl;
    if (url.startsWith("//")) {
        url = "https:" + url; // Add protocol for protocol-relative URLs
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("User-Agent", "IMSLP Search Dialog/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("listItem", QVariant::fromValue(static_cast<void *>(item)));

    // Connect this specific reply to the slot
    connect(reply, &QNetworkReply::finished, this, &IMSLPSearchDialog::onThumbnailDownloaded);
}

void IMSLPSearchDialog::onThumbnailDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    reply->deleteLater();

    // Get the associated list item
    void *itemPtr = reply->property("listItem").value<void *>();
    auto *item = static_cast<QListWidgetItem *>(itemPtr);

    if (!item) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray imageData = reply->readAll();
        QPixmap pixmap;

        if (pixmap.loadFromData(imageData)) {
            // Scale to fit within 100px height while maintaining aspect ratio
            QPixmap scaledPixmap = pixmap.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(scaledPixmap));
        }
    }
    // If download failed, keep the default gray icon
}
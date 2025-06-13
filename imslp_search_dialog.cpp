#include "imslp_search_dialog.h"
#include "imslp_client.h"
#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>
#include <QtConcurrent/QtConcurrent>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineDownloadRequest>

IMSLPSearchDialog::IMSLPSearchDialog(QWidget *parent)
    : QDialog(parent)
    , m_client(std::make_unique<IMSLPClient>())
    , m_networkManager(new QNetworkAccessManager(this))
    , m_webView(nullptr)
{
    setupUI();
    setWindowTitle("IMSLP Search");
    setMinimumSize(600, 400);
    resize(800, 600);
    setModal(false);
}

IMSLPSearchDialog::~IMSLPSearchDialog()
{
    if (m_webView) {
        m_webView->deleteLater();
        m_webView = nullptr;
    }
}

void IMSLPSearchDialog::setupWebEngine()
{
    qDebug() << "Setting up WebEngine...";

    // Create WebEngine view
    m_webView = new QWebEngineView();
    m_webView->setWindowTitle("IMSLP Download");
    m_webView->resize(800, 600);

    qDebug() << "WebView configured successfully";

    // Set a timeout to prevent hanging
    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(30000); // 30 second timeout

    connect(timeoutTimer, &QTimer::timeout, [this]() {
        qDebug() << "WebEngine load timeout";
        m_statusLabel->setText("Download timeout - please try again");
        if (m_webView) {
            m_webView->stop();
            m_webView->hide();
        }
    });

    qDebug() << "About to connect WebEngine signals...";

    // Connect download requests
    connect(m_webView->page()->profile(), &QWebEngineProfile::downloadRequested,
            this, &IMSLPSearchDialog::onWebEngineDownloadRequested);

    // Connect load finished to detect when JavaScript has executed
    connect(m_webView, &QWebEngineView::loadFinished,
            this, &IMSLPSearchDialog::onWebEngineLoadFinished);

    // Connect load started to start timeout
    connect(m_webView, &QWebEngineView::loadStarted, [this, timeoutTimer]() {
        qDebug() << "WebEngine load started";
        timeoutTimer->start();
    });

    // Stop timeout when finished
    connect(m_webView, &QWebEngineView::loadFinished, [timeoutTimer](bool) {
        timeoutTimer->stop();
    });

    qDebug() << "WebEngine setup completed successfully";
}

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

    // View control buttons
    QHBoxLayout *viewControlLayout = new QHBoxLayout();

    m_listViewButton = new QPushButton("List");
    m_listViewButton->setCheckable(true);
    m_listViewButton->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    m_gridViewButton = new QPushButton("Grid");
    m_gridViewButton->setCheckable(true);
    m_gridViewButton->setChecked(true); // Default to grid view
    m_gridViewButton->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    m_smallIconButton = new QPushButton("Small");
    m_smallIconButton->setCheckable(true);
    m_smallIconButton->setChecked(true); // Default to small icons
    m_smallIconButton->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    m_largeIconButton = new QPushButton("Large");
    m_largeIconButton->setCheckable(true);
    m_largeIconButton->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    viewControlLayout->addWidget(new QLabel("View:"));
    viewControlLayout->addWidget(m_listViewButton);
    viewControlLayout->addWidget(m_gridViewButton);
    viewControlLayout->addSpacing(20);
    viewControlLayout->addWidget(new QLabel("Size:"));
    viewControlLayout->addWidget(m_smallIconButton);
    viewControlLayout->addWidget(m_largeIconButton);
    viewControlLayout->addStretch();

    mainLayout->addLayout(viewControlLayout);

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
    connect(m_listViewButton, &QPushButton::clicked, this, &IMSLPSearchDialog::onListViewClicked);
    connect(m_gridViewButton, &QPushButton::clicked, this, &IMSLPSearchDialog::onGridViewClicked);
    connect(m_smallIconButton, &QPushButton::clicked, this, &IMSLPSearchDialog::onSmallIconClicked);
    connect(m_largeIconButton, &QPushButton::clicked, this, &IMSLPSearchDialog::onLargeIconClicked);
    connect(m_resultsList, &QListWidget::itemDoubleClicked, this, &IMSLPSearchDialog::onItemDoubleClicked);

    updateViewMode();
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
    int iconSize = getIconSize();
    QPixmap defaultPixmap(iconSize, iconSize);
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
            // Store the full-size pixmap in the item data for later rescaling
            item->setData(Qt::UserRole + 2, pixmap);

            // Scale to current icon size
            int iconSize = getIconSize();
            QPixmap scaledPixmap = pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(scaledPixmap));
        }
    }
    // If download failed, keep the default gray icon
}

void IMSLPSearchDialog::onListViewClicked()
{
    m_viewMode = ViewMode::List;
    m_listViewButton->setChecked(true);
    m_gridViewButton->setChecked(false);
    updateViewMode();
}

void IMSLPSearchDialog::onGridViewClicked()
{
    m_viewMode = ViewMode::Grid;
    m_listViewButton->setChecked(false);
    m_gridViewButton->setChecked(true);
    updateViewMode();
}

void IMSLPSearchDialog::onSmallIconClicked()
{
    m_iconSize = IconSize::Small;
    m_smallIconButton->setChecked(true);
    m_largeIconButton->setChecked(false);
    updateViewMode();
}

void IMSLPSearchDialog::onLargeIconClicked()
{
    m_iconSize = IconSize::Large;
    m_smallIconButton->setChecked(false);
    m_largeIconButton->setChecked(true);
    updateViewMode();
}

void IMSLPSearchDialog::updateViewMode()
{
    int iconSize = getIconSize();

    if (m_viewMode == ViewMode::List) {
        m_resultsList->setViewMode(QListView::ListMode);
        m_resultsList->setIconSize(QSize(iconSize, iconSize));
    } else {
        m_resultsList->setViewMode(QListView::IconMode);
        m_resultsList->setIconSize(QSize(iconSize, iconSize));
        m_resultsList->setResizeMode(QListView::Adjust);
        m_resultsList->setMovement(QListView::Static);
    }

    // Re-scale existing thumbnails using stored full-size pixmaps
    for (int i = 0; i < m_resultsList->count(); ++i) {
        QListWidgetItem *item = m_resultsList->item(i);
        if (item) {
            QPixmap fullSizePixmap = item->data(Qt::UserRole + 2).value<QPixmap>();
            if (!fullSizePixmap.isNull()) {
                QPixmap scaledPixmap = fullSizePixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(QIcon(scaledPixmap));
            }
        }
    }
}

int IMSLPSearchDialog::getIconSize() const
{
    return (m_iconSize == IconSize::Small) ? 100 : 200;
}

void IMSLPSearchDialog::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;

    QString pdfUrl = item->data(Qt::UserRole).toString();
    qDebug() << "PDF URL from item:" << pdfUrl;

    if (pdfUrl.isEmpty()) {
        m_statusLabel->setText("No PDF URL found for this item");
        return;
    }

    downloadAndOpenPdf(item);
}

void IMSLPSearchDialog::downloadAndOpenPdf(QListWidgetItem *item)
{
    if (!item) return;

    QString filename = item->text();
    QString pdfUrl = item->data(Qt::UserRole).toString();

    qDebug() << "Starting WebEngine download for filename:" << filename;
    qDebug() << "PDF URL:" << pdfUrl;

    // Create and setup WebEngine if not already done
    if (!m_webView) {
        setupWebEngine();
    }

    // Clean the PDF URL - add https if it starts with //
    if (pdfUrl.startsWith("//")) {
        pdfUrl = "https:" + pdfUrl;
    }

    // Clean filename for saving
    QString cleanFilename = filename;
    if (!cleanFilename.endsWith(".pdf", Qt::CaseInsensitive)) {
        cleanFilename += ".pdf";
    }
    cleanFilename = cleanFilename.replace(QRegularExpression("[<>:\"/\\|?*]"), "_");

    m_currentDownloadPath = getTempFilePath(cleanFilename);
    m_currentDownloadFilename = cleanFilename;

    qDebug() << "Will save to:" << m_currentDownloadPath;

    m_statusLabel->setText(QString("Loading %1 with JavaScript...").arg(cleanFilename));

    // Show the browser and load the URL
    m_webView->show();
    m_webView->load(QUrl(pdfUrl));
}

void IMSLPSearchDialog::onWebEngineLoadFinished(bool success)
{
    qDebug() << "WebEngine load finished. Success:" << success;
    qDebug() << "Current URL:" << m_webView->url().toString();

    if (!success) {
        qDebug() << "WebEngine load failed";
        m_statusLabel->setText("Failed to load page - please try again");
        return;
    }

    // Check if we're now at a PDF URL or if we need to wait for more JavaScript
    QString currentUrl = m_webView->url().toString();
    if (currentUrl.endsWith(".pdf", Qt::CaseInsensitive)) {
        qDebug() << "WebEngine resolved to PDF URL, should trigger download automatically";
        m_statusLabel->setText(QString("Downloading %1...").arg(m_currentDownloadFilename));
    } else {
        qDebug() << "WebEngine loaded non-PDF page, checking content";
        m_statusLabel->setText(QString("Processing JavaScript for %1...").arg(m_currentDownloadFilename));

        // Add a delay before executing JavaScript to let page fully load
        QTimer::singleShot(2000, this, [this]() {
            qDebug() << "Executing JavaScript to analyze page";

            // Execute JavaScript to check if we need to trigger any actions
            m_webView->page()->runJavaScript("document.documentElement.outerHTML", [this](const QVariant &result) {
                QString html = result.toString();
                qDebug() << "Page HTML length:" << html.length();
                qDebug() << "HTML preview:" << html.left(500);

                // Look for PDF links or download triggers in the page
                if (html.contains("allowAccess") || html.contains("BOT_DETECT")) {
                    qDebug() << "Found bot detection, waiting for JavaScript to execute";
                    m_statusLabel->setText("Bot detection found, waiting for JavaScript...");
                    // The JavaScript should automatically execute and trigger a reload/redirect
                } else if (html.contains(".pdf")) {
                    qDebug() << "Found PDF references in page";
                    m_statusLabel->setText("Found PDF links, attempting to click...");
                    // Look for clickable download links
                    m_webView->page()->runJavaScript(
                        "var links = document.querySelectorAll('a[href$=\".pdf\"]');"
                        "console.log('Found', links.length, 'PDF links');"
                        "if (links.length > 0) { "
                        "  console.log('Clicking first PDF link:', links[0].href);"
                        "  links[0].click(); "
                        "}"
                    );
                } else {
                    qDebug() << "No PDF links or bot detection found";
                    m_statusLabel->setText("No download links found - may need manual intervention");
                }
            });
        });
    }
}

void IMSLPSearchDialog::onWebEngineDownloadRequested(QWebEngineDownloadRequest *download)
{
    qDebug() << "WebEngine download requested:" << download->url().toString();
    qDebug() << "Suggested filename:" << download->suggestedFileName();
    qDebug() << "Download state:" << static_cast<int>(download->state());

    // Set the download path
    QString downloadDir = QFileInfo(m_currentDownloadPath).absolutePath();
    QString downloadFile = QFileInfo(m_currentDownloadPath).fileName();

    qDebug() << "Setting download directory to:" << downloadDir;
    qDebug() << "Setting download filename to:" << downloadFile;

    download->setDownloadDirectory(downloadDir);
    download->setDownloadFileName(downloadFile);

    // Connect to download progress and completion
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, [this, download]() {
        qDebug() << "Download finished changed. Is finished:" << download->isFinished();
        qDebug() << "Download state:" << static_cast<int>(download->state());

        if (download->isFinished()) {
            if (download->state() == QWebEngineDownloadRequest::DownloadCompleted) {
                qDebug() << "WebEngine download completed successfully";
                qDebug() << "Downloaded file path:" << download->downloadDirectory() + "/" + download->downloadFileName();

                // Open in MusicReader
                MusicReader *mainWindow = qobject_cast<MusicReader *>(parent());
                if (mainWindow) {
                    mainWindow->open_pdf_in_tab(m_currentDownloadPath.toStdString());
                    m_statusLabel->setText("PDF opened in MusicReader");
                    qDebug() << "PDF opened in MusicReader";

                    // Hide the browser after successful download
                    if (m_webView) {
                        m_webView->hide();
                    }
                } else {
                    qDebug() << "Parent window not found";
                    m_statusLabel->setText("Could not open PDF - parent window not found");
                }
            } else {
                qDebug() << "WebEngine download failed with state:" << static_cast<int>(download->state());
                m_statusLabel->setText("Download failed - please try again");

                // Hide browser on failure too
                if (m_webView) {
                    m_webView->hide();
                }
            }
        }
    });

    // Accept and start the download
    qDebug() << "Accepting download request";
    download->accept();
    m_statusLabel->setText(QString("Downloading %1...").arg(m_currentDownloadFilename));
}

QString IMSLPSearchDialog::getTempFilePath(const QString &filename) const
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(tempDir).filePath(filename);
}
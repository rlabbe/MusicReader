#include "imslp_search_dialog.h"
#include "imslp_client.h"
#include <QtWidgets/QtWidgets>
#include <QtNetwork/QtNetwork>
#include <QtConcurrent/QtConcurrent>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineDownloadRequest>
#include "logger.h"

IMSLPSearchDialog::IMSLPSearchDialog(MusicReader *parent)
    : QDialog(parent)
    , client_(std::make_unique<IMSLPClient>())
    , network_manager_(new QNetworkAccessManager(this))
    , web_view_(nullptr)
{
    setup_ui();
    setWindowTitle("IMSLP Search");
    setMinimumSize(600, 400);
    resize(800, 600);
    setModal(false);
}

IMSLPSearchDialog::~IMSLPSearchDialog()
{
    if (web_view_) {
        web_view_->deleteLater();
        web_view_ = nullptr;
    }
}

void IMSLPSearchDialog::setup_web_engine()
{
    // Create WebEngine view
    web_view_ = new QWebEngineView();
    web_view_->setWindowTitle("IMSLP Download");
    web_view_->resize(800, 600);

    // Connect to detect when user closes the browser window
    connect(web_view_, &QObject::destroyed, this, [this]() {
        status_label_->setText("Download cancelled");
        web_view_ = nullptr; // Reset pointer since object is being destroyed
    });

    // Set a timeout to prevent hanging
    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(30000); // 30 second timeout

    connect(timeoutTimer, &QTimer::timeout, [this]() {
        status_label_->setText("Download timeout - please try again");
        if (web_view_) {
            web_view_->stop();
            web_view_->deleteLater();
            web_view_ = nullptr;
        }
    });

    // Connect download requests
    connect(web_view_->page()->profile(), &QWebEngineProfile::downloadRequested,
            this, &IMSLPSearchDialog::on_web_engine_download_requested);

    // Connect load finished to detect when JavaScript has executed
    connect(web_view_, &QWebEngineView::loadFinished,
            this, &IMSLPSearchDialog::on_web_engine_load_finished);

    // Connect load started to start timeout
    connect(web_view_, &QWebEngineView::loadStarted, [this, timeoutTimer]() {
        timeoutTimer->start();
    });

    // Stop timeout when finished
    connect(web_view_, &QWebEngineView::loadFinished, [timeoutTimer](bool) {
        timeoutTimer->stop();
    });
}


void IMSLPSearchDialog::setup_ui()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Search section
    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));

    search_edit_ = new QLineEdit();
    search_edit_->setPlaceholderText("Enter composer, work title, or BWV number...");
    searchLayout->addWidget(search_edit_);

    search_button_ = new QPushButton("Search");
    searchLayout->addWidget(search_button_);

    mainLayout->addLayout(searchLayout);

    // View control buttons
    QHBoxLayout *viewControlLayout = new QHBoxLayout();

    list_view_button_ = new QPushButton("List");
    list_view_button_->setCheckable(true);
    list_view_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    grid_view_button_ = new QPushButton("Grid");
    grid_view_button_->setCheckable(true);
    grid_view_button_->setChecked(true); // Default to grid view
    grid_view_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    small_icon_button_ = new QPushButton("Small");
    small_icon_button_->setCheckable(true);
    small_icon_button_->setChecked(true); // Default to small icons
    small_icon_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    large_icon_button_ = new QPushButton("Large");
    large_icon_button_->setCheckable(true);
    large_icon_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    viewControlLayout->addWidget(new QLabel("View:"));
    viewControlLayout->addWidget(list_view_button_);
    viewControlLayout->addWidget(grid_view_button_);
    viewControlLayout->addSpacing(20);
    viewControlLayout->addWidget(new QLabel("Size:"));
    viewControlLayout->addWidget(small_icon_button_);
    viewControlLayout->addWidget(large_icon_button_);
    viewControlLayout->addStretch();

    mainLayout->addLayout(viewControlLayout);

    // Status section
    status_label_ = new QLabel("Enter search terms and click Search");
    mainLayout->addWidget(status_label_);

    progress_bar_ = new QProgressBar();
    progress_bar_->setVisible(false);
    mainLayout->addWidget(progress_bar_);

    // Results section
    mainLayout->addWidget(new QLabel("Results:"));

    results_list_ = new QListWidget();
    results_list_->setIconSize(QSize(100, 100));
    mainLayout->addWidget(results_list_);

    // Connect signals
    connect(search_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_search_clicked);
    connect(search_edit_, &QLineEdit::returnPressed, this, &IMSLPSearchDialog::on_search_clicked);
    connect(list_view_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_list_view_clicked);
    connect(grid_view_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_grid_view_clicked);
    connect(small_icon_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_small_icon_clicked);
    connect(large_icon_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_large_icon_clicked);
    connect(results_list_, &QListWidget::itemDoubleClicked, this, &IMSLPSearchDialog::on_item_double_clicked);

    update_view_mode();
}

void IMSLPSearchDialog::on_search_clicked()
{
    QString searchText = search_edit_->text().trimmed();
    if (searchText.isEmpty()) {
        QMessageBox::warning(this, "Search", "Please enter search terms");
        return;
    }

    clear_results();
    search_button_->setEnabled(false);
    progress_bar_->setVisible(true);
    progress_bar_->setRange(0, 0); // Indeterminate progress
    status_label_->setText("Searching...");

    // Run search in background thread
    auto *watcher = new QFutureWatcher<std::vector<FileInfo>>(this);
    connect(watcher, &QFutureWatcher<std::vector<FileInfo>>::finished, [this, watcher]() {
        try {
            auto results = watcher->result();
            current_results_ = std::move(results);

            progress_bar_->setVisible(false);
            search_button_->setEnabled(true);

            if (current_results_.empty()) {
                status_label_->setText("No results found");
            } else {
                status_label_->setText(QString("Found %1 PDF(s)").arg(current_results_.size()));

                // Add results to list
                for (const auto &pdf : current_results_) {
                    add_result_to_list(pdf);
                }
            }
        } catch (const std::exception &e) {
            progress_bar_->setVisible(false);
            search_button_->setEnabled(true);
            status_label_->setText("Search failed");
            QMessageBox::critical(this, "Search Error", QString("Search failed: %1").arg(e.what()));
        }
        watcher->deleteLater();
    });

    QFuture<std::vector<FileInfo>> future = QtConcurrent::run([this, searchText]() {
        return client_->get_work_pdfs(searchText.toStdString());
    });

    watcher->setFuture(future);
}

void IMSLPSearchDialog::clear_results()
{
    results_list_->clear();
    current_results_.clear();
}

void IMSLPSearchDialog::add_result_to_list(const FileInfo &pdf)
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
    int iconSize = get_icon_size();
    QPixmap defaultPixmap(iconSize, iconSize);
    defaultPixmap.fill(Qt::lightGray);
    item->setIcon(QIcon(defaultPixmap));

    results_list_->addItem(item);

    // Download thumbnail if available
    if (!pdf.thumb_url.empty()) {
        download_thumbnail(item, QString::fromStdString(pdf.thumb_url));
    }
}

void IMSLPSearchDialog::download_thumbnail(QListWidgetItem *item, const QString &thumb_url)
{
    QString url = thumb_url;
    if (url.startsWith("//")) {
        url = "https:" + url; // Add protocol for protocol-relative URLs
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("User-Agent", "IMSLP Search Dialog/1.0");

    QNetworkReply *reply = network_manager_->get(request);
    reply->setProperty("listItem", QVariant::fromValue(static_cast<void *>(item)));

    // Connect this specific reply to the slot
    connect(reply, &QNetworkReply::finished, this, &IMSLPSearchDialog::on_thumbnail_downloaded);
}

void IMSLPSearchDialog::on_thumbnail_downloaded()
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
            int iconSize = get_icon_size();
            QPixmap scaledPixmap = pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(scaledPixmap));
        }
    }
    // If download failed, keep the default gray icon
}

void IMSLPSearchDialog::on_list_view_clicked()
{
    view_mode_ = ViewMode::List;
    list_view_button_->setChecked(true);
    grid_view_button_->setChecked(false);
    update_view_mode();
}

void IMSLPSearchDialog::on_grid_view_clicked()
{
    view_mode_ = ViewMode::Grid;
    list_view_button_->setChecked(false);
    grid_view_button_->setChecked(true);
    update_view_mode();
}

void IMSLPSearchDialog::on_small_icon_clicked()
{
    icon_size_ = IconSize::Small;
    small_icon_button_->setChecked(true);
    large_icon_button_->setChecked(false);
    update_view_mode();
}

void IMSLPSearchDialog::on_large_icon_clicked()
{
    icon_size_ = IconSize::Large;
    small_icon_button_->setChecked(false);
    large_icon_button_->setChecked(true);
    update_view_mode();
}

void IMSLPSearchDialog::update_view_mode()
{
    int iconSize = get_icon_size();

    if (view_mode_ == ViewMode::List) {
        results_list_->setViewMode(QListView::ListMode);
        results_list_->setIconSize(QSize(iconSize, iconSize));
    } else {
        results_list_->setViewMode(QListView::IconMode);
        results_list_->setIconSize(QSize(iconSize, iconSize));
        results_list_->setResizeMode(QListView::Adjust);
        results_list_->setMovement(QListView::Static);
    }

    // Re-scale existing thumbnails using stored full-size pixmaps
    for (int i = 0; i < results_list_->count(); ++i) {
        QListWidgetItem *item = results_list_->item(i);
        if (item) {
            QPixmap fullSizePixmap = item->data(Qt::UserRole + 2).value<QPixmap>();
            if (!fullSizePixmap.isNull()) {
                QPixmap scaledPixmap = fullSizePixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(QIcon(scaledPixmap));
            }
        }
    }
}

int IMSLPSearchDialog::get_icon_size() const
{
    return (icon_size_ == IconSize::Small) ? 100 : 200;
}

void IMSLPSearchDialog::on_item_double_clicked(QListWidgetItem *item)
{
    if (!item) return;

    QString pdfUrl = item->data(Qt::UserRole).toString();
    if (pdfUrl.isEmpty()) {
        status_label_->setText("No PDF URL found for this item");
        return;
    }

    download_and_open_pdf(item);
}

void IMSLPSearchDialog::download_and_open_pdf(QListWidgetItem *item)
{
    if (!item) return;

    QString filename = item->text();
    QString pdfUrl = item->data(Qt::UserRole).toString();

    // Create and setup WebEngine if not already done
    if (!web_view_) {
        setup_web_engine();
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

    current_download_path_ = get_temp_file_path(cleanFilename);
    current_download_filename_ = cleanFilename;

    status_label_->setText(QString("Loading %1 with JavaScript...").arg(cleanFilename));

    // Show the browser and load the URL
    web_view_->show();
    web_view_->load(QUrl(pdfUrl));
}

void IMSLPSearchDialog::on_web_engine_load_finished(bool success)
{
    if (!success) {
        status_label_->setText("Failed to load page - please try again");
        return;
    }

    // Check if we're now at a PDF URL or if we need to wait for more JavaScript
    QString currentUrl = web_view_->url().toString();
    if (currentUrl.endsWith(".pdf", Qt::CaseInsensitive)) {
        status_label_->setText(QString("Downloading %1...").arg(current_download_filename_));
    } else {
        status_label_->setText(QString("Processing JavaScript for %1...").arg(current_download_filename_));

        // Add a delay before executing JavaScript to let page fully load
        QTimer::singleShot(2000, this, [this]() {
            // Execute JavaScript to check if we need to trigger any actions
            web_view_->page()->runJavaScript("document.documentElement.outerHTML", [this](const QVariant &result) {
                QString html = result.toString();

                // Look for PDF links or download triggers in the page
                if (html.contains("allowAccess") || html.contains("BOT_DETECT")) {
                    status_label_->setText("Bot detection found, waiting for JavaScript...");
                    // The JavaScript should automatically execute and trigger a reload/redirect
                } else if (html.contains(".pdf")) {
                    status_label_->setText("Found PDF links, attempting to click...");
                    // Look for clickable download links
                    web_view_->page()->runJavaScript(
                        "var links = document.querySelectorAll('a[href$=\".pdf\"]');"
                        "console.log('Found', links.length, 'PDF links');"
                        "if (links.length > 0) { "
                        "  console.log('Clicking first PDF link:', links[0].href);"
                        "  links[0].click(); "
                        "}"
                    );
                } else {
                    status_label_->setText("No download links found - may need manual intervention");
                }
            });
        });
    }
}

void IMSLPSearchDialog::on_web_engine_download_requested(QWebEngineDownloadRequest *download)
{
    // Set the download path
    QString downloadDir = QFileInfo(current_download_path_).absolutePath();
    QString downloadFile = QFileInfo(current_download_path_).fileName();

    download->setDownloadDirectory(downloadDir);
    download->setDownloadFileName(downloadFile);

    // Connect to download progress and completion
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, [this, download]() {

        if (download->isFinished()) {
            if (download->state() == QWebEngineDownloadRequest::DownloadCompleted) {

                // Open in MusicReader
                MusicReader *mainWindow = qobject_cast<MusicReader *>(parent());
                if (mainWindow) {
                    mainWindow->open_pdf_in_tab(current_download_path_.toStdString());
                    status_label_->setText("PDF opened in MusicReader");

                    // Delete the browser after successful download
                    if (web_view_) {
                        web_view_->deleteLater();
                        web_view_ = nullptr;
                    }
                } else {
                    status_label_->setText("Could not open PDF - parent window not found");
                }
            } else {
                status_label_->setText("Download failed - please try again");

                // Delete browser on failure too
                if (web_view_) {
                    web_view_->deleteLater();
                    web_view_ = nullptr;
                }
            }
        }
    });

    // Accept and start the download
    download->accept();
    status_label_->setText(QString("Downloading %1...").arg(current_download_filename_));
}

QString IMSLPSearchDialog::get_temp_file_path(const QString &filename) const
{
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(tempDir).filePath(filename);
}
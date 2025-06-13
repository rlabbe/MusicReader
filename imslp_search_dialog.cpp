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
    , file_prefixes_({ "PMLP", "IMSLP" })
    , hover_timer_(new QTimer(this))
    , hover_popup_(nullptr)
{
    setup_ui();
    setWindowTitle("IMSLP Search");
    setMinimumSize(600, 400);
    resize(800, 600);
    setModal(false);
}

IMSLPSearchDialog::~IMSLPSearchDialog()
{
    if (hover_popup_) {
        hover_popup_->close();
        hover_popup_->deleteLater();
        hover_popup_ = nullptr;
    }
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
        web_view_ = nullptr;
    });

    // Set a timeout to prevent hanging
    QTimer *timeout_timer = new QTimer(this);
    timeout_timer->setSingleShot(true);
    timeout_timer->setInterval(30000); // 30 second timeout

    connect(timeout_timer, &QTimer::timeout, [this]() {
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
    connect(web_view_, &QWebEngineView::loadStarted, [this, timeout_timer]() {
        timeout_timer->start();
    });

    // Stop timeout when finished
    connect(web_view_, &QWebEngineView::loadFinished, [timeout_timer](bool) {
        timeout_timer->stop();
    });
}


void IMSLPSearchDialog::setup_ui()
{
    auto *main_layout = new QVBoxLayout(this);

    // Search section
    auto *search_layout = new QHBoxLayout();
    search_layout->addWidget(new QLabel("Search:"));

    search_edit_ = new QLineEdit();
    search_edit_->setPlaceholderText("Enter composer, work title, or BWV number...");
    search_layout->addWidget(search_edit_);

    search_button_ = new QPushButton("Search");
    search_layout->addWidget(search_button_);

    main_layout->addLayout(search_layout);

    // View control buttons
    QHBoxLayout *view_control_layout = new QHBoxLayout();

    list_view_button_ = new QPushButton("List");
    list_view_button_->setCheckable(true);
    list_view_button_->setChecked(true); // Default to list view
    list_view_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    grid_view_button_ = new QPushButton("Grid");
    grid_view_button_->setCheckable(true);
    grid_view_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    small_icon_button_ = new QPushButton("Small");
    small_icon_button_->setCheckable(true);
    small_icon_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");
    large_icon_button_ = new QPushButton("Large");
    large_icon_button_->setCheckable(true);
    large_icon_button_->setChecked(true); // Default to large icons
    large_icon_button_->setStyleSheet("QPushButton:checked { background-color: lightblue; }");

    view_control_layout->addWidget(new QLabel("View:"));
    view_control_layout->addWidget(list_view_button_);
    view_control_layout->addWidget(grid_view_button_);
    view_control_layout->addSpacing(20);
    view_control_layout->addWidget(new QLabel("Size:"));
    view_control_layout->addWidget(small_icon_button_);
    view_control_layout->addWidget(large_icon_button_);
    view_control_layout->addStretch();

    main_layout->addLayout(view_control_layout);

    // Status section
    status_label_ = new QLabel("Enter search terms and click Search");
    main_layout->addWidget(status_label_);

    progress_bar_ = new QProgressBar();
    progress_bar_->setVisible(false);
    main_layout->addWidget(progress_bar_);

    // Results section
    main_layout->addWidget(new QLabel("Results:"));

    results_list_ = new QListWidget();
    results_list_->setIconSize(QSize(200, 200));
    results_list_->setViewMode(QListView::ListMode);
    results_list_->setFlow(QListView::TopToBottom);
    results_list_->setWrapping(false);
    results_list_->setMouseTracking(true);
    results_list_->viewport()->setMouseTracking(true);
    results_list_->viewport()->installEventFilter(this);
    main_layout->addWidget(results_list_);

    // Connect signals
    connect(search_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_search_clicked);
    connect(search_edit_, &QLineEdit::returnPressed, this, &IMSLPSearchDialog::on_search_clicked);
    connect(list_view_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_list_view_clicked);
    connect(grid_view_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_grid_view_clicked);
    connect(small_icon_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_small_icon_clicked);
    connect(large_icon_button_, &QPushButton::clicked, this, &IMSLPSearchDialog::on_large_icon_clicked);
    connect(results_list_, &QListWidget::itemDoubleClicked, this, &IMSLPSearchDialog::on_item_double_clicked);

    // Set initial view mode
    view_mode_ = ViewMode::List;
    icon_size_ = IconSize::Large;

    update_view_mode();
}

void IMSLPSearchDialog::on_search_clicked()
{
    QString search_text = search_edit_->text().trimmed();
    if (search_text.isEmpty()) {
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

    QFuture<std::vector<FileInfo>> future = QtConcurrent::run([this, search_text]() {
        return client_->get_work_pdfs(search_text.toStdString());
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
    QString display_name = QString::fromStdString(pdf.filename);
    if (display_name.startsWith("File:"))
        display_name = display_name.mid(5);

    // Strip prefixes if present
    for (const auto &prefix : file_prefixes_) {
        QRegularExpression regex("^" + QString::fromStdString(prefix) + "\\d+-\\s*");
        display_name = display_name.replace(regex, "");
    }

    auto *item = new QListWidgetItem(display_name);
    item->setData(Qt::UserRole, QString::fromStdString(pdf.url));
    item->setData(Qt::UserRole + 1, QString::fromStdString(pdf.thumb_url));

    int icon_size = get_icon_size();
    QPixmap default_pixmap(icon_size, icon_size);
    default_pixmap.fill(Qt::lightGray);
    item->setIcon(QIcon(default_pixmap));

    results_list_->addItem(item);

    if (!pdf.thumb_url.empty())
        download_thumbnail(item, QString::fromStdString(pdf.thumb_url));
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
    void *item_ptr = reply->property("listItem").value<void *>();
    auto *item = static_cast<QListWidgetItem *>(item_ptr);

    if (!item) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray image_data = reply->readAll();
        QPixmap pixmap;

        if (pixmap.loadFromData(image_data)) {
            // Store the full-size pixmap in the item data for later rescaling
            item->setData(Qt::UserRole + 2, pixmap);

            // Scale to current icon size
            int icon_size = get_icon_size();
            QPixmap scaled_pixmap = pixmap.scaled(icon_size, icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            item->setIcon(QIcon(scaled_pixmap));
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
    int icon_size = get_icon_size();

    if (view_mode_ == ViewMode::List) {
        results_list_->setViewMode(QListView::ListMode);
        results_list_->setIconSize(QSize(icon_size, icon_size));
        results_list_->setFlow(QListView::TopToBottom);
        results_list_->setWrapping(false);
        results_list_->setResizeMode(QListView::Adjust);
        results_list_->setMovement(QListView::Static);
    } else {
        results_list_->setViewMode(QListView::IconMode);
        results_list_->setIconSize(QSize(icon_size, icon_size));
        results_list_->setResizeMode(QListView::Adjust);
        results_list_->setMovement(QListView::Static);
        results_list_->setFlow(QListView::LeftToRight);
        results_list_->setWrapping(true);
        results_list_->setGridSize(QSize(icon_size + 50, icon_size + 80));
        results_list_->setUniformItemSizes(true);
    }

    // Re-scale existing thumbnails using stored full-size pixmaps
    for (int i = 0; i < results_list_->count(); ++i) {
        QListWidgetItem *item = results_list_->item(i);
        if (item) {
            QPixmap full_size_pixmap = item->data(Qt::UserRole + 2).value<QPixmap>();
            if (!full_size_pixmap.isNull()) {
                QPixmap scaled_pixmap = full_size_pixmap.scaled(icon_size, icon_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(QIcon(scaled_pixmap));
            }
        }
    }
}

int IMSLPSearchDialog::get_icon_size() const
{
    return (icon_size_ == IconSize::Large) ? 200 : 100;
}

void IMSLPSearchDialog::on_item_double_clicked(QListWidgetItem *item)
{
    if (!item) return;

    QString pdf_url = item->data(Qt::UserRole).toString();
    if (pdf_url.isEmpty()) {
        status_label_->setText("No PDF URL found for this item");
        return;
    }

    download_and_open_pdf(item);
}

void IMSLPSearchDialog::download_and_open_pdf(QListWidgetItem *item)
{
    if (!item) return;

    QString filename = item->text();
    QString pdf_url = item->data(Qt::UserRole).toString();

    // Create and setup WebEngine if not already done
    if (!web_view_) {
        setup_web_engine();
    }

    // Clean the PDF URL - add https if it starts with //
    if (pdf_url.startsWith("//")) {
        pdf_url = "https:" + pdf_url;
    }

    // Clean filename for saving
    QString clean_filename = filename;
    if (!clean_filename.endsWith(".pdf", Qt::CaseInsensitive)) {
        clean_filename += ".pdf";
    }
    clean_filename = clean_filename.replace(QRegularExpression("[<>:\"/\\|?*]"), "_");

    current_download_path_ = get_temp_file_path(clean_filename);
    current_download_filename_ = clean_filename;

    status_label_->setText(QString("Loading %1 with JavaScript...").arg(clean_filename));

    // Show the browser and load the URL
    web_view_->show();
    web_view_->load(QUrl(pdf_url));
}

void IMSLPSearchDialog::on_web_engine_load_finished(bool success)
{
    if (!success) {
        status_label_->setText("Failed to load page - please try again");
        return;
    }

    // Check if we're now at a PDF URL or if we need to wait for more JavaScript
    QString current_url = web_view_->url().toString();
    if (current_url.endsWith(".pdf", Qt::CaseInsensitive)) {
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
    QString download_dir = QFileInfo(current_download_path_).absolutePath();
    QString download_file = QFileInfo(current_download_path_).fileName();

    download->setDownloadDirectory(download_dir);
    download->setDownloadFileName(download_file);

    // Connect to download progress and completion
    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, [this, download]() {

        if (download->isFinished()) {
            if (download->state() == QWebEngineDownloadRequest::DownloadCompleted) {

                // Open in MusicReader
                MusicReader *main_window = qobject_cast<MusicReader *>(parent());
                if (main_window) {
                    main_window->open_pdf_in_tab(current_download_path_.toStdString(), 1, nullptr, true);
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
    QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(temp_dir).filePath(filename);
}

bool IMSLPSearchDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == results_list_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            if (mouse_event->button() == Qt::RightButton) {
                QListWidgetItem *item = results_list_->itemAt(mouse_event->pos());
                if (item) {
                    hide_hover_popup();
                    hover_item_ = item;
                    show_hover_popup();
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (hover_popup_ && hover_popup_->isVisible()) {
                hide_hover_popup();
                hover_item_ = nullptr;
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        if (hover_popup_ && hover_popup_->isVisible()) {
            hide_hover_popup();
            hover_item_ = nullptr;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void IMSLPSearchDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && hover_popup_ && hover_popup_->isVisible()) {
        hide_hover_popup();
        hover_item_ = nullptr;
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void IMSLPSearchDialog::show_hover_popup()
{
    if (!hover_item_)
        return;

    // Use the full-size pixmap stored in UserRole + 2, not the scaled icon
    QPixmap full_size_pixmap = hover_item_->data(Qt::UserRole + 2).value<QPixmap>();

    if (full_size_pixmap.isNull())
        return;

    if (hover_popup_) {
        hover_popup_->deleteLater();
        hover_popup_ = nullptr;
    }

    hover_popup_ = new QLabel(nullptr);
    hover_popup_->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    hover_popup_->setStyleSheet("border: 2px solid black; background-color: white;");
    hover_popup_->setAttribute(Qt::WA_DeleteOnClose);
    hover_popup_->installEventFilter(this);

    // Scale image to fit screen while preserving aspect ratio
    QRect screen_geometry = QGuiApplication::primaryScreen()->geometry();
    int max_height = screen_geometry.height() * 0.75; // 75% of screen height
    int max_width = screen_geometry.width() - 100;    // Leave some margin for width

    // Only scale if the image is larger than the available space
    QPixmap scaled_pixmap = full_size_pixmap;
    if (full_size_pixmap.width() > max_width || full_size_pixmap.height() > max_height) {
        scaled_pixmap = full_size_pixmap.scaled(max_width, max_height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    hover_popup_->setPixmap(scaled_pixmap);
    hover_popup_->resize(scaled_pixmap.size());

    QPoint center_pos = screen_geometry.center() - QPoint(hover_popup_->width() / 2, hover_popup_->height() / 2);

    hover_popup_->move(center_pos);
    hover_popup_->show();
    hover_popup_->raise();
    hover_popup_->activateWindow();
}

void IMSLPSearchDialog::hide_hover_popup()
{
    if (hover_popup_) {
        hover_popup_->hide();
        hover_popup_->deleteLater();
        hover_popup_ = nullptr;
    }
}
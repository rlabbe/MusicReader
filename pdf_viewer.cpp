#include "pdf_viewer.h"
#include <QApplication>
#include <QDebug>
#include "border.h"



PDFViewer::PDFViewer(Document *document, ConfigFile *config, int page, StatusBar *sbar, QWidget *parent)
    : QWidget(parent)
    , document_(document)
    , status_bar_(sbar)
    , config_(config)
    , initial_page_num_(page)
    , drawing_margin_(false)
{

    setFocusPolicy(Qt::StrongFocus);
    init_ui(page);

    //TODO Qt6 might use QEvent::ApplicationPaletteChange
    /*
         void changeEvent(QEvent *event) override {
        if (event->type() == QEvent::ApplicationPaletteChange) {
            // Handle palette change here
            QPalette newPalette = QApplication::palette();
            // ... use newPalette ...
        }
        QWidget::changeEvent(event);
    }

    * */

    //connect(qobject_cast<QGuiApplication *>(QCoreApplication::instance()), &QGuiApplication::paletteChanged, this, &PDFViewer::refresh);

    if (document_->page_count() > 0)
    {
        get_page(initial_page_num_, true);
    }
}

int PDFViewer::page_count() const
{
    return document_->page_count();
}

int PDFViewer::current_page() const
{
    return initial_page_num_;
}

bool PDFViewer::single_page_view() const
{
    return config_->page_view_count == 1;
}

void PDFViewer::replace_document(Document *document, int page)
{
    document_ = document;
    initial_page_num_ = page;
    get_page(page);
}

void PDFViewer::refresh()
{
    get_page(initial_page_num_);
    update_scrollbar_visibility();
}

void PDFViewer::page_up()
{
    change_page(single_page_view() ? -1 : -2);
}

void PDFViewer::page_down()
{
    change_page(single_page_view() ? 1 : 2);
}

void PDFViewer::change_page(int step)
{
    int new_page = qBound(1, current_page() + step, page_count());
    scrollbar_->setValue(new_page);
    get_page(new_page);
}

void PDFViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_PageUp:
    case Qt::Key_Up:
        page_up();
        break;
    case Qt::Key_PageDown:
    case Qt::Key_Down:
    case Qt::Key_Space:
        page_down();
        break;
    case Qt::Key_Left:
        change_page(-1);
        break;
    case Qt::Key_Right:
        change_page(1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void PDFViewer::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0)
    {
        page_up();
    }
    else
    {
        page_down();
    }
}

bool PDFViewer::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture)
    {
        auto *gesture = dynamic_cast<QSwipeGesture *>(static_cast<QGestureEvent *>(event)->gesture(Qt::SwipeGesture));

        if (gesture->horizontalDirection() == QSwipeGesture::Left)
        {
            page_down();
        }
        else
        {
            page_up();
        }
        return true;
    }
    return QWidget::event(event);
}

void PDFViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    _update_image();
}

void PDFViewer::init_ui(int page)
{
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);

    label_ = new QLabel(this);
    label_->setStyleSheet("border: 0px;");
    layout_->addWidget(label_, 1);

    scrollbar_ = new QScrollBar(Qt::Vertical, this);
    scrollbar_->setMinimum(1);
    scrollbar_->setMaximum(page_count());
    scrollbar_->setValue(page);
    connect(scrollbar_, &QScrollBar::valueChanged, this, &PDFViewer::on_scrollbar_value_changed);
    layout_->addWidget(scrollbar_);

    setLayout(layout_);
    update_scrollbar_visibility();
}

void PDFViewer::on_scrollbar_value_changed(int new_page)
{
    if (new_page != current_page())
    {
        get_page(new_page);
    }
}

void PDFViewer::update_scrollbar_visibility()
{
    bool all_pages_shown = (page_count() == 1) || (page_count() == 2 && single_page_view());
    scrollbar_->setVisible(!all_pages_shown);
}

void PDFViewer::get_page(int page_num, bool first_call)
{
    if (document_->page_count() == 0) return;

    if (single_page_view() || page_num == document_->page_count())
    {
        page_ = get_single_page(page_num);
    }
    else
    {
        page_ = get_double_page(page_num);
    }

    _update_image();
    if (!first_call)
    {
        adjust_subwindow_size();
    }
    else
    {
        adjust_initial_subwindow_size();
    }
}

QPixmap PDFViewer::get_single_page(int page_num)
{
    std::optional<QPixmap> opt_pixmap = document_->get_page(page_num);
    return opt_pixmap.value_or(QPixmap());
}

QPixmap PDFViewer::get_double_page(int page_num)
{
    auto p1 = document_->get_page(page_num);
    auto p2 = document_->get_page(page_num + 1);
    if (!p1 || !p2) return QPixmap();

    int line_width = 8;
    int combined_width = p1->width() + p2->width() + line_width;
    int max_height = std::max(p1->height(), p2->height());

    QPixmap combined_image(combined_width, max_height);
    combined_image.fill(QApplication::palette().color(QPalette::Window));

    QPainter painter(&combined_image);
    painter.drawPixmap(0, (max_height - p1->height()) / 2, *p1);
    painter.drawPixmap(p1->width() + line_width, (max_height - p2->height()) / 2, *p2);

    painter.setPen(QPen(QApplication::palette().color(QPalette::Window), line_width));
    painter.drawLine(p1->width(), 0, p1->width(), max_height);

    return combined_image;
}


void PDFViewer::_update_image(const QString &message)
{
    if (page_.isNull())
    {
        label_->setText(message.isEmpty() ? "Loading..." : message);
        label_->setAlignment(Qt::AlignCenter);
        label_->setStyleSheet("background-color: white; color: black; font-size: 16pt;");
        return;
    }

    label_->setStyleSheet("");

    QSize max_size = config_->allow_oversize ? label_->size() : page_.size().boundedTo(label_->size());
    label_->setPixmap(page_.scaled(max_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    label_->setAlignment(Qt::AlignTop | Qt::AlignCenter);

    label_->setScaledContents(true);  // Let Qt handle scaling efficiently

    Border border = find_content_edges(page_);

    // Set margins to "crop" the displayed region (instead of copying the pixmap)
    label_->setContentsMargins(-border.left, -border.top,
                               -(page_.width() - border.right),
                               -(page_.height() - border.bottom));

    label_->setAlignment(Qt::AlignTop | Qt::AlignCenter);
}

void PDFViewer::adjust_initial_subwindow_size()
{
    if (page_.isNull()) return;

    QSize max_size = parentWidget()->size();
    QSize scaled_size = page_.size().scaled(max_size, Qt::KeepAspectRatio);
    resize(scaled_size);
    label_->resize(scaled_size);
    setMinimumSize(1, 1);
}

void PDFViewer::adjust_subwindow_size()
{
    if (page_.isNull()) return;

    QSize max_size = parentWidget()->size();
    QSize new_size = QSize(std::min(page_.width(), max_size.width()), std::min(page_.height(), max_size.height()));
    resize(new_size);
}

#include "detached_window.h"

#include "horizontal_tabs_widget.h"
#include "vertical_tabs_widget.h"
#include "pdf_viewer.h"
#include "status_bar.h"

DetachedWindow::DetachedWindow(bool horizontal_tabs)
{
    setAttribute(Qt::WA_DeleteOnClose);

    if (horizontal_tabs)
        tab_widget_ = new HorizontalTabWidget(this);
    else
        tab_widget_ = new VerticalTabsWidget(this);

    tab_widget_->setTabsClosable(true);

    status_bar_ = new StatusBar(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tab_widget_);
    layout->addWidget(status_bar_);
}

void DetachedWindow::closeEvent(QCloseEvent* event)
{
    for (int i = 0; i < tab_widget_->count(); ++i)
    {
        QWidget* page = tab_widget_->widget(i);
        if (!page)
            continue;

        auto* viewer = page->findChild<PDFViewer*>();
        if (viewer)
            viewer->closing();
    }

    QWidget::closeEvent(event);
}

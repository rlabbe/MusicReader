#pragma once

#include <QtWidgets>

class StatusBar : public QStatusBar {
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr) : QStatusBar(parent)
    {
        auto make_label = []() {
            QLabel *label = new QLabel("");
            label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
            return label;
        };

        page_combo_box_ = new QComboBox(this);
        page_combo_box_->setEditable(false);
        page_combo_box_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        addPermanentWidget(page_combo_box_);

        memory_usage_ = make_label();
        addPermanentWidget(memory_usage_);

        // TODO: connect to paletteChanged signal
        // connect(qobject_cast<QGuiApplication *>(QCoreApplication::instance()), &QGuiApplication::paletteChanged, this, &StatusBar::update_style);
    }

    void set_memory_usage(const std::string &msg)
    {
        memory_usage_->setText(QString::fromStdString(msg));
    }

    void set_page_count(int current_page, int total_pages)
    {
        if (total_pages > 0) {
            page_combo_box_->blockSignals(true);
            page_combo_box_->clear();
            for (int i = 0; i < total_pages; ++i)
                page_combo_box_->addItem(QString::number(i + 1));
            page_combo_box_->setCurrentIndex(current_page - 1);
            page_combo_box_->adjustSize();
            page_combo_box_->blockSignals(false);
        } else {
            clear_page_count();
        }
    }

    void clear_page_count()
    {
        page_combo_box_->blockSignals(true);
        page_combo_box_->clear();
        page_combo_box_->adjustSize();
        page_combo_box_->blockSignals(false);
    }

    void update_style()
    {
        QPalette palette = QGuiApplication::palette();
        QString base_color = palette.color(QPalette::Window).name();
        QString shadow_color = palette.color(QPalette::Shadow).name();

        QString label_style = QString("QLabel { background-color: %1; border: 2px solid %2; border-style: ridge; padding: 2px; margin-right: 5px; }")
            .arg(base_color, shadow_color);

        QString combo_style = QString("QComboBox { background-color: %1; border: 1px solid %2; padding: 2px; margin-right: 5px; }")
            .arg(base_color, shadow_color);

        memory_usage_->setStyleSheet(label_style);
        page_combo_box_->setStyleSheet(combo_style);
    }


    QComboBox *page_combo_box_;
    QLabel *memory_usage_;
};

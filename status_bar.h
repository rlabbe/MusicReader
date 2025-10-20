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

        memory_usage_ = make_label();
        addPermanentWidget(memory_usage_);
    }


    void set_memory_usage(const std::string &msg)
    {
        memory_usage_->setText(QString::fromStdString(msg));
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
    }

    QLabel *memory_usage_;
};

#pragma once

#include <QTextEdit>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include "font_info.h" 


class InPlaceAnnotationEditor : public QTextEdit {
    Q_OBJECT

public:
    InPlaceAnnotationEditor(const FontInfo &font_info, QWidget *parent = nullptr);
    void start_editing(const QPoint &position, const QString &initial_text = "");

signals:
    void editing_finished(const QString &text);
    void editing_cancelled();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void finish_editing();
    void cancel_editing();

    QString font_family_;
    float font_size_;
    QColor font_color_;
    bool editing_finished_ = false;
};
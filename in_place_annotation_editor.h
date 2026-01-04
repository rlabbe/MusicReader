#pragma once

#include <QtWidgets>
#include "font_info.h"

class ConfigFile;

class InPlaceAnnotationEditor : public QTextEdit {
    Q_OBJECT

public:
    InPlaceAnnotationEditor(ConfigFile* config, QWidget* parent = nullptr);
    void start_editing(const QPoint& position, const QString& initial_text = "");
    void set_dpi_scale(float scale) { dpi_scale_ = scale; }

signals:
    void editing_finished(const QString& text);
    void editing_cancelled();

private slots:
    void resize_to_content();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void finish_editing();
    void cancel_editing();

    ConfigFile* config_ = nullptr;
    bool editing_finished_ = false;
    float dpi_scale_ = 1.0f;
};

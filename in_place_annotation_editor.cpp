#include "in_place_annotation_editor.h"
#include "annotation_coords.h"
#include "config_file.h"
#include "logger.h"
#include <iostream>


InPlaceAnnotationEditor::InPlaceAnnotationEditor(ConfigFile* config, QWidget* parent)
    : QTextEdit(parent)
    , config_(config)
{
    setFrameStyle(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::NoWrap);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    const FontInfo& font_info = config_->annotation_font();
    QString qt_font_name = pdf_font_to_qt_font(font_info.family);
    QFont font(qt_font_name, static_cast<int>(font_info.size));
    setFont(font);

    auto [r, g, b] = font_info.color;
    QString color_str = QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
    setStyleSheet(QString("background-color: transparent; border: none; color: %1;").arg(color_str));

    connect(this, &QTextEdit::textChanged, this, &InPlaceAnnotationEditor::on_text_changed);

    hide();
}


void InPlaceAnnotationEditor::start_editing(const QPoint& position, const QString& initial_text)
{
    editing_finished_ = false;
    setReadOnly(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);

    const FontInfo& font_info = config_->annotation_font();
    QString qt_font_name = pdf_font_to_qt_font(font_info.family);
    QFont scaled_font(qt_font_name);
    scaled_font.setPixelSize(static_cast<int>(font_info.size * dpi_scale_));
    setFont(scaled_font);

    auto [r2, g2, b2] = font_info.color;
    QString color_str = QString("rgb(%1, %2, %3)").arg(r2).arg(g2).arg(b2);
    setStyleSheet(QString("background-color: transparent; border: none; color: %1;").arg(color_str));

    setPlainText(initial_text);

    FontInfo scaled_font_info = font_info;
    scaled_font_info.size *= dpi_scale_;

    QSize size;
    if (initial_text.isEmpty())
        size = calculate_text_size("A", scaled_font_info);
    else
        size = calculate_text_size(initial_text, scaled_font_info);

    resize(size);

    // Position editor so baseline aligns with click point
    int doc_margin = static_cast<int>(document()->documentMargin());
    QFontMetrics fm(scaled_font);

    QPoint editor_pos;
    editor_pos.setX(position.x());
    editor_pos.setY(AnnotationCoordinates::baseline_display_to_editor_widget_y(position.y(), doc_margin, fm.ascent()));

    move(editor_pos);

    show();
    setFocus();
    selectAll();
}

void InPlaceAnnotationEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finish_editing();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancel_editing();
        return;
    }
    QTextEdit::keyPressEvent(event);

    resize_to_content();
}

void InPlaceAnnotationEditor::focusOutEvent(QFocusEvent* event)
{
    // qDebug() << "focusOutEvent triggered";
    finish_editing();
    QTextEdit::focusOutEvent(event);
}

void InPlaceAnnotationEditor::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
    if (config_->debug_annotations()) {
        QPainter painter(viewport());
        painter.setPen(QPen(Qt::black, 1, Qt::DotLine));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}


void InPlaceAnnotationEditor::finish_editing()
{
    if (!editing_finished_)
        emit editing_finished(toPlainText());
    editing_finished_ = true;

    if (config_->debug_annotations()) {
        setReadOnly(true);
        setStyleSheet(QString("background-color: transparent; border: none; color: blue;"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true); // Prevent editor from blocking clicks
    } else {
        hide();
        setAttribute(Qt::WA_TransparentForMouseEvents, true); // Prevent hidden editor from blocking clicks
    }
}

void InPlaceAnnotationEditor::cancel_editing()
{
    if (!editing_finished_)
        emit editing_cancelled();
    editing_finished_ = true;
    hide();
}

void InPlaceAnnotationEditor::resize_to_content()
{
    FontInfo scaled_font_info = config_->annotation_font();
    scaled_font_info.size *= dpi_scale_;

    QSize new_size = calculate_text_size(toPlainText(), scaled_font_info);

    QPoint current_pos = pos();
    resize(new_size);
    move(current_pos);
}


void InPlaceAnnotationEditor::on_text_changed()
{
    if (!editing_finished_)
        emit text_changed_for_preview(toPlainText());
}


void InPlaceAnnotationEditor::set_preview_mode(bool enabled)
{
    preview_mode_ = enabled;
    if (enabled) {
        // Make text invisible so only MuPDF preview shows, but keep cursor visible
        setStyleSheet("background-color: transparent; border: none; color: transparent;");
        // Keep cursor visible by making it opaque
        setCursorWidth(2);
    } else {
        // Restore normal text color
        const FontInfo& font_info = config_->annotation_font();
        auto [r, g, b] = font_info.color;
        QString color_str = QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
        setStyleSheet(QString("background-color: transparent; border: none; color: %1;").arg(color_str));
    }
}

#include "in_place_annotation_editor.h"
#include "annotation_coords.h"
#include "config_file.h"
#include "logger.h"
#include <iostream>

// InPlaceAnnotationEditor Design
// ==============================
//
// This is a transparent text input widget overlaid on the PDF view. Its sole purpose
// is to capture keystrokes and provide a cursor - the actual text rendering is done
// by MuPDF. MuPDF and Qt use different font metrics, so Qt-rendered text won't match
// the final PDF annotation position/size
//
// How it works:
// 1. User clicks on PDF in annotation mode -> start_editing() positions this widget
// 2. As user types, text_changed_for_preview signal triggers MuPDF to render a preview
// 3. set_preview_mode(true) makes Qt's text invisible (color: transparent) so only
//    MuPDF's rendered preview shows through
// 4. The widget still displays the cursor, allowing the user to see where they're typing
// 5. On Enter/focus loss, editing_finished signal sends final text to create the annotation
//
// Key signals:
// - text_changed_for_preview: emitted on each keystroke for live MuPDF preview
// - editing_finished: emitted when user presses Enter or widget loses focus
// - escape_pressed: emitted to exit annotation mode entirely

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
    float scaled_font_size = font_info.size * dpi_scale_;

    // Set Qt font for cursor display (text will be transparent in preview mode)
    QString qt_font_name = pdf_font_to_qt_font(font_info.family);
    QFont scaled_font(qt_font_name);
    scaled_font.setPixelSize(static_cast<int>(scaled_font_size));
    setFont(scaled_font);

    auto [r, g, b] = font_info.color;
    QString color_str = QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
    setStyleSheet(QString("background-color: transparent; border: none; color: %1;").arg(color_str));

    setPlainText(initial_text);

    // Size widget using MuPDF metrics to match rendered preview
    std::string measure_text = initial_text.isEmpty() ? "M" : initial_text.toStdString();
    float width = text_width(font_info.family, scaled_font_size, measure_text);
    float height = text_height(font_info.family, scaled_font_size);

    int doc_margin = static_cast<int>(document()->documentMargin());
    int widget_width = static_cast<int>(width) + 2 * doc_margin + 4;
    int widget_height = static_cast<int>(height) + 2 * doc_margin + 4;
    resize(widget_width, widget_height);

    // Position editor so baseline aligns with click point, using MuPDF ascent
    float ascent = font_ascent(font_info.family, scaled_font_size);
    QPoint editor_pos;
    editor_pos.setX(position.x());
    editor_pos.setY(
        AnnotationCoordinates::baseline_display_to_editor_widget_y(position.y(), doc_margin, static_cast<int>(ascent)));

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
        emit escape_pressed(); // Signal to exit annotation mode BEFORE finishing (prevents re-entry)
        finish_editing();
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
    if (editing_finished_)
        return;
    editing_finished_ = true;
    emit editing_finished(toPlainText());

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
    const FontInfo& font_info = config_->annotation_font();
    float scaled_font_size = font_info.size * dpi_scale_;

    // Size widget using MuPDF metrics to match rendered preview
    QString text = toPlainText();
    std::string measure_text = text.isEmpty() ? "M" : text.toStdString();
    float width = text_width(font_info.family, scaled_font_size, measure_text);
    float height = text_height(font_info.family, scaled_font_size);

    int doc_margin = static_cast<int>(document()->documentMargin());
    int widget_width = static_cast<int>(width) + 2 * doc_margin + 4;
    int widget_height = static_cast<int>(height) + 2 * doc_margin + 4;

    QPoint current_pos = pos();
    resize(widget_width, widget_height);
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

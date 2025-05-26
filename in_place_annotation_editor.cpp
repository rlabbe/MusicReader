#include "in_place_annotation_editor.h"

InPlaceAnnotationEditor::InPlaceAnnotationEditor(const FontInfo &font_info, QWidget *parent)
    : QTextEdit(parent), font_family_(font_info.family), font_size_(font_info.size), font_color_(font_info.color)
{
    setFrameStyle(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    QFont font(font_family_, static_cast<int>(font_size_));
    setFont(font);

    QString color_str = QString("rgb(%1, %2, %3)").arg(font_color_.red()).arg(font_color_.green()).arg(font_color_.blue());
    setStyleSheet(QString("background-color: transparent; border: none; color: %1;").arg(color_str));

    hide();
}



void InPlaceAnnotationEditor::start_editing(const QPoint &position, const QString &initial_text)
{
    setPlainText(initial_text);

    FontInfo current_font{ font().family(), static_cast<float>(font().pointSize()), palette().color(QPalette::Text) };
    QSize size = calculate_text_size(initial_text, current_font);
    resize(size);

    // Position editor so text appears exactly at click point
    QPoint editor_pos = position;
    int doc_margin = static_cast<int>(document()->documentMargin());

    editor_pos.setX(position.x() - doc_margin - 2);
    editor_pos.setY(position.y() - doc_margin - 2);

    move(editor_pos);

    show();
    setFocus();
    selectAll();
}

void InPlaceAnnotationEditor::keyPressEvent(QKeyEvent *event)
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
}

void InPlaceAnnotationEditor::focusOutEvent(QFocusEvent *event)
{
    qDebug() << "focusOutEvent triggered";
    finish_editing();
    QTextEdit::focusOutEvent(event);
}

void InPlaceAnnotationEditor::paintEvent(QPaintEvent *event)
{
    QTextEdit::paintEvent(event);

    QPainter painter(viewport());
    painter.setPen(QPen(Qt::black, 1, Qt::DotLine));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}



void InPlaceAnnotationEditor::finish_editing()
{
    if (!editing_finished_) emit editing_finished(toPlainText());
    editing_finished_ = true;
    hide();
}

void InPlaceAnnotationEditor::cancel_editing()
{
    if (!editing_finished_) emit editing_cancelled();
    editing_finished_ = true;
    hide();
}
#pragma once
#include <QtWidgets>
#include <iostream>

class FileViewer : public QMainWindow {
    Q_OBJECT

public:
    explicit FileViewer(const QString &filename, QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle(filename);
        setAttribute(Qt::WA_DeleteOnClose);

        auto *textEdit = new QTextEdit(this);
        textEdit->setReadOnly(true);
        setCentralWidget(textEdit);

        QFile file(filename);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            textEdit->setPlainText(QString::fromUtf8(file.readAll()));
        }

        resize(800, 600);
    }
};


#pragma once
#include <QtWidgets>
#include <iostream>
#include <string>

class FileViewer : public QMainWindow {
    Q_OBJECT

public:
    explicit FileViewer(const std::string &filename, QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        QString q_filename = QString::fromStdString(filename);

        setWindowTitle(q_filename);
        setAttribute(Qt::WA_DeleteOnClose);

        auto *textEdit = new QTextEdit(this);
        textEdit->setReadOnly(true);
        setCentralWidget(textEdit);

        QFile file(q_filename);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            textEdit->setPlainText(QString::fromUtf8(file.readAll()));
        }

        resize(800, 600);
    }
};


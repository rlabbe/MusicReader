#pragma once
#include <QtWidgets>
#include <iostream>
#include <string>

class FileViewer : public QMainWindow {
    Q_OBJECT
public:
    explicit FileViewer(const std::string &filename, QWidget *parent = nullptr)
        : QMainWindow(parent), m_filename(filename)
    {
        QString q_filename = QString::fromStdString(filename);
        setWindowTitle(q_filename);
        setAttribute(Qt::WA_DeleteOnClose);

        auto *centralWidget = new QWidget(this);
        auto *layout = new QVBoxLayout(centralWidget);

        auto *buttonLayout = new QHBoxLayout();
        auto *refreshButton = new QPushButton("Refresh", this);
        connect(refreshButton, &QPushButton::clicked, this, &FileViewer::loadFile);
        buttonLayout->addWidget(refreshButton);

        auto *clearButton = new QPushButton("Clear", this);
        connect(clearButton, &QPushButton::clicked, this, &FileViewer::clearFile);
        buttonLayout->addWidget(clearButton);

        buttonLayout->addStretch();
        layout->addLayout(buttonLayout);

        m_textEdit = new QTextEdit(this);
        m_textEdit->setReadOnly(true);
        layout->addWidget(m_textEdit);

        setCentralWidget(centralWidget);
        loadFile();
        resize(800, 600);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_F) {
            showSearchDialog();
            return;
        }
        if (event->key() == Qt::Key_F3) {
            if (event->modifiers() & Qt::ShiftModifier)
                searchPrevious();
            else
                searchNext();
            return;
        }
        QMainWindow::keyPressEvent(event);
    }

private slots:
    void loadFile()
    {
        QString q_filename = QString::fromStdString(m_filename);
        QFile file(q_filename);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_textEdit->setPlainText(QString::fromUtf8(file.readAll()));
            m_textEdit->moveCursor(QTextCursor::End);
        }
    }

    void clearFile()
    {
        QString q_filename = QString::fromStdString(m_filename);
        QFile file(q_filename);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.close();
            m_textEdit->clear();
        }
    }

    void showSearchDialog()
    {
        bool ok;
        QString text = QInputDialog::getText(this, "Search", "Find:", QLineEdit::Normal, m_searchText, &ok);
        if (ok && !text.isEmpty()) {
            m_searchText = text;
            searchNext();
        }
    }

    void searchNext()
    {
        if (!m_searchText.isEmpty())
            m_textEdit->find(m_searchText);
    }

    void searchPrevious()
    {
        if (!m_searchText.isEmpty())
            m_textEdit->find(m_searchText, QTextDocument::FindBackward);
    }

private:
    std::string m_filename;
    QTextEdit *m_textEdit;
    QString m_searchText;
};
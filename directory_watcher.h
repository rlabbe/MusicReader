#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QString>
#include <QOperatingSystemVersion>
#include <QDir>
#include <QDirIterator>


class EventHandler : public QFileSystemWatcher {
    Q_OBJECT

public:
    explicit EventHandler(const QString &file_ending, int debounce_time_ms = 500)
        : file_ending_(file_ending), debounce_time_(debounce_time_ms)
    {
        connect(&timer_, &QTimer::timeout, this, &EventHandler::emit_signal);
        timer_.setSingleShot(true);
    }

    void on_file_changed(const QString &path)
    {
        if (path.endsWith(file_ending_)) {
            start_timer();
        }
    }

    void on_directory_changed()
    {
        start_timer();
    }

signals:
    void file_changed_signal();

private:
    void start_timer()
    {
        timer_.start(debounce_time_);
    }

    void emit_signal()
    {
        emit file_changed_signal();
    }

    QString file_ending_;
    QTimer timer_;
    int debounce_time_;
};

class DirectoryWatcher : public QObject {
    Q_OBJECT

public:
    explicit DirectoryWatcher(const QString &file_ending, QObject *parent = nullptr)
        : QObject(parent), file_ending_(file_ending)
    {
        supported_ = QOperatingSystemVersion::currentType() != QOperatingSystemVersion::Unknown;
        if (supported_) {
            event_handler_ = new EventHandler(file_ending_);
            connect(event_handler_, &EventHandler::file_changed_signal, this, &DirectoryWatcher::file_changed);
        }
    }

    void start(const QString &directory)
    {
        if (!supported_) return;

        stop();
        directory_ = directory;
        if (!directory.isEmpty()) {
            add_directories_recursively(directory);
            connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &DirectoryWatcher::on_directory_changed);
            connect(&watcher_, &QFileSystemWatcher::fileChanged, event_handler_, &EventHandler::on_file_changed);
        }
    }

    void stop()
    {
        if (!supported_) return;

        if (!watcher_.directories().isEmpty()) watcher_.removePaths(watcher_.directories());
        if (!watcher_.files().isEmpty())  watcher_.removePaths(watcher_.files());
    }

signals:
    void file_changed();

private:
    void add_directories_recursively(const QString &directory)
    {
        QDirIterator it(directory, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        watcher_.addPath(directory);

        while (it.hasNext()) {
            watcher_.addPath(it.next());
        }
    }

    void on_directory_changed()
    {
        // Rescan the entire tree to handle new/deleted directories
        if (!directory_.isEmpty()) {
            QStringList current_dirs = watcher_.directories();
            watcher_.removePaths(current_dirs);
            add_directories_recursively(directory_);
        }
        event_handler_->on_directory_changed();
    }

    bool supported_;
    QString file_ending_;
    QString directory_;
    QFileSystemWatcher watcher_;
    EventHandler *event_handler_;
};
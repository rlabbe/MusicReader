#pragma once

#include <QtWidgets>

class ConfigFile;

class DevStatusDialog : public QDialog {
    Q_OBJECT

public:
    static void show(ConfigFile &config, QWidget *parent);
    static void close_if_open();
    ~DevStatusDialog();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void update_status();

private:
    DevStatusDialog(ConfigFile &config, QWidget *parent);

    struct SystemStats {
        size_t memory_usage_bytes;
        size_t total_memory_bytes;
        double cpu_percentage;
        int thread_count;
    };

    void setup_ui();
    SystemStats get_system_stats();
    QString format_memory(size_t bytes);

    QTextEdit *status_display_ = nullptr;
    QTimer *update_timer_ = nullptr;
    ConfigFile &config_;

    void *cpu_query_ = nullptr;
    void *cpu_counter_ = nullptr;
    bool cpu_initialized_ = false;
    QWidget *parent_widget_ = nullptr;

    static DevStatusDialog *instance_;
    static constexpr int UPDATE_INTERVAL_MS = 1000;
};
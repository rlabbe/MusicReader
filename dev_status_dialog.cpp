#include "dev_status_dialog.h"
#include "config_file.h"
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QShowEvent>
#include <QApplication>
#include <QScreen>
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <thread>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "pdh.lib")

QString format_page_ranges(const std::vector<int>& pages);

DevStatusDialog* DevStatusDialog::instance_ = nullptr;


void DevStatusDialog::show(ConfigFile& config, QWidget* parent)
{
    if (!instance_)
        instance_ = new DevStatusDialog(config, parent);
    instance_->QDialog::show();
    instance_->raise();
    instance_->activateWindow();
}


void DevStatusDialog::close_if_open()
{
    if (instance_)
        instance_->close();
}


DevStatusDialog::DevStatusDialog(ConfigFile& config, QWidget* parent)
    : QDialog(nullptr)
    , config_(config)
    , parent_widget_(parent)
{
    setWindowTitle("Developer Status");
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);
    instance_ = this;

    setup_ui();

    // Initialize CPU monitoring for this process
    wchar_t counter_path[512];
    swprintf_s(counter_path, L"\\Process(MusicReader)\\%% Processor Time");

    if (PdhOpenQuery(nullptr, 0, reinterpret_cast<PDH_HQUERY*>(&cpu_query_)) == ERROR_SUCCESS) {
        if (PdhAddCounterW(reinterpret_cast<PDH_HQUERY>(cpu_query_), counter_path, 0,
                           reinterpret_cast<PDH_HCOUNTER*>(&cpu_counter_)) == ERROR_SUCCESS) {
            PdhCollectQueryData(reinterpret_cast<PDH_HQUERY>(cpu_query_));
            cpu_initialized_ = true;
        }
    }

    update_timer_ = new QTimer(this);
    connect(update_timer_, &QTimer::timeout, this, &DevStatusDialog::update_status);
    update_timer_->start(UPDATE_INTERVAL_MS);

    update_status();
}


DevStatusDialog::~DevStatusDialog()
{
    instance_ = nullptr;

    if (cpu_query_)
        PdhCloseQuery(reinterpret_cast<PDH_HQUERY>(cpu_query_));
}


void DevStatusDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // Get current geometry from config each time
    const auto& dialog_size = config_.dev_dialog_size();
    if (dialog_size.size() >= 4 && dialog_size[2] > 0 && dialog_size[3] > 0) {
        resize(dialog_size[2], dialog_size[3]);
        move(dialog_size[0], dialog_size[1]);
    } else {
        // Default size and center on parent widget (or screen if no parent)
        resize(500, 500);
        if (parent_widget_) {
            QRect parent_rect = parent_widget_->geometry();
            move(parent_rect.center() - rect().center());
        } else {
            QRect screen = QApplication::primaryScreen()->geometry();
            move(screen.center() - rect().center());
        }
    }
}

void DevStatusDialog::closeEvent(QCloseEvent* event)
{
    // Save size and position to config when dialog is closed
    QRect geom = geometry();
    std::array<int, 4> dialog_size = {geom.x(), geom.y(), geom.width(), geom.height()};
    config_.set_dev_size(dialog_size);

    QDialog::closeEvent(event);
}

void DevStatusDialog::setup_ui()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    status_display_ = new QTextEdit(this);
    status_display_->setReadOnly(true);
    layout->addWidget(status_display_);

    setLayout(layout);
}

DevStatusDialog::SystemStats DevStatusDialog::get_system_stats()
{
    SystemStats stats = {};

    // Get memory info
    PROCESS_MEMORY_COUNTERS mem_info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &mem_info, sizeof(mem_info)))
        stats.memory_usage_bytes = mem_info.WorkingSetSize;

    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status))
        stats.total_memory_bytes = mem_status.ullTotalPhys;

    // Get CPU usage using PDH
    if (cpu_initialized_) {
        if (PdhCollectQueryData(reinterpret_cast<PDH_HQUERY>(cpu_query_)) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE counter_value;
            if (PdhGetFormattedCounterValue(reinterpret_cast<PDH_HCOUNTER>(cpu_counter_), PDH_FMT_DOUBLE, nullptr,
                                            &counter_value) == ERROR_SUCCESS) {
                // Divide by number of cores to match Task Manager's per-core average
                stats.cpu_percentage = counter_value.doubleValue / std::thread::hardware_concurrency();
            }
        }
    }

    // Get thread count
    DWORD thread_count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(snapshot, &te32)) {
            DWORD current_process_id = GetCurrentProcessId();
            do {
                if (te32.th32OwnerProcessID == current_process_id)
                    thread_count++;
            } while (Thread32Next(snapshot, &te32));
        }
        CloseHandle(snapshot);
    }
    stats.thread_count = static_cast<int>(thread_count);

    return stats;
}


QString DevStatusDialog::format_memory(size_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }

    return QString("%1 %2").arg(size, 0, 'f', 3).arg(units[unit_index]);
}


void DevStatusDialog::update_status()
{
    // Save scroll position
    QScrollBar* scroll_bar = status_display_->verticalScrollBar();
    int scroll_position = scroll_bar->value();

    auto system_stats = get_system_stats();

    QString html = "<html><body>";

    // System stats
    html += "<h4>System Resources</h4>";
    html += "<table border='1' cellpadding='5' cellspacing='0'>";
    html += "<tr><th>Resource</th><th>Value</th></tr>";

    int memory_pct = static_cast<int>(100.0 * system_stats.memory_usage_bytes / system_stats.total_memory_bytes);
    html += QString("<tr><td>Process Memory</td><td>%1 (%2%)</td></tr>")
                .arg(format_memory(system_stats.memory_usage_bytes))
                .arg(memory_pct);

    html += QString("<tr><td>Process CPU</td><td>%1%</td></tr>").arg(system_stats.cpu_percentage, 0, 'f', 1);

    html += QString("<tr><td>Process Threads</td><td>%1</td></tr>").arg(system_stats.thread_count);

    html += "</table>";
    html += "</body></html>";
    status_display_->setHtml(html);

    // Restore scroll position
    scroll_bar->setValue(scroll_position);
}
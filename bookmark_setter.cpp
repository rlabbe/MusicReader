#include "bookmark_setter.h"
#include <windows.h>
#include <string>
#include <mutex>
#include <iostream>


namespace BookmarkSetter {

namespace {
HANDLE stdin_write_ = nullptr;
HANDLE stdout_read_ = nullptr;
HANDLE proc_ = nullptr;
std::mutex proc_mutex_;
}

bool startup()
{
    std::lock_guard<std::mutex> lock(proc_mutex_);
    if (proc_) return true;

    SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

    HANDLE stdin_read = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write_, &sa, 0)) return false;
    if (!SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0)) return false;

    HANDLE stdout_write = nullptr;
    if (!CreatePipe(&stdout_read_, &stdout_write, &sa, 0)) return false;
    if (!SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0)) return false;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring exe_path = L"\"set_bookmarks.exe\"";

    BOOL success = CreateProcessW(
        nullptr,
        exe_path.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!success) {
        DWORD err = GetLastError();
        std::wcerr << L"CreateProcessW failed: " << err << std::endl;
        LPWSTR msg = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                       nullptr, err, 0, (LPWSTR)&msg, 0, nullptr);
        std::wcerr << L"Error: " << msg << std::endl;
        LocalFree(msg);
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);

    if (!success) {
        stdin_write_ = nullptr;
        stdout_read_ = nullptr;
        return false;
    }

    proc_ = pi.hProcess;
    CloseHandle(pi.hThread);
    return true;
}

bool send(const std::filesystem::path &filename, const std::string &bookmark_data)
{
    std::lock_guard<std::mutex> lock(proc_mutex_);
    if (!proc_ || !stdin_write_ || !stdout_read_) return false;

    std::string command = filename.string() + '\t' + bookmark_data + "\n";

    DWORD written = 0;
    if (!WriteFile(stdin_write_, command.data(), (DWORD)command.size(), &written, nullptr)) 
        return false;

    char buffer[512]{};
    DWORD read = 0;
    std::string response;

    while (true) {
        BOOL success = ReadFile(stdout_read_, buffer, sizeof(buffer) - 1, &read, nullptr);
        if (!success || read == 0) break;

        buffer[read] = 0;
        response += buffer;

        if (response.find('\n') != std::string::npos) break;
    }

    return response.starts_with("OK");
}


void shutdown()
{
    std::lock_guard<std::mutex> lock(proc_mutex_);
    if (!proc_) return;

    if (stdin_write_) {
        CloseHandle(stdin_write_);
        stdin_write_ = nullptr;
    }

    if (stdout_read_) {
        CloseHandle(stdout_read_);
        stdout_read_ = nullptr;
    }

    if (WaitForSingleObject(proc_, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(proc_, 0);
    }

    CloseHandle(proc_);
    proc_ = nullptr;
}

} 

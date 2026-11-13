#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <filesystem>
#include <iostream>
#include "config_file.h"
#include "exception_logger.h"

#ifdef _WIN32
#include <Windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif
#include <cstdint>
#include <cstdlib>
#include <exception>


// put here to avoid including spdlog.h in the header file
static std::shared_ptr<spdlog::logger> logger_;


namespace {

#ifdef _WIN32
std::string process_executable_path()
{
    wchar_t buffer[MAX_PATH] {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0)
        return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string path(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), path.data(), needed, nullptr, nullptr);
    return path;
}


// Names one address as module+offset and, when the pdb is next to the exe, as
// symbol+offset.
std::string describe_address(DWORD64 address)
{
    std::string text = std::format("0x{:016X}", address);

    IMAGEHLP_MODULE64 module {};
    module.SizeOfStruct = sizeof(module);
    if (SymGetModuleInfo64(GetCurrentProcess(), address, &module) && module.BaseOfImage != 0)
        text += std::format(" {}+0x{:X}", std::string_view(module.ModuleName), address - module.BaseOfImage);

    char storage[sizeof(SYMBOL_INFO) + 512] {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol))
        text += std::format(" {}+0x{:X}", std::string_view(symbol->Name), displacement);

    return text;
}


// The bytes the cpu was trying to execute, which often says what kind of
// object was being touched.
std::string format_instruction_bytes(DWORD64 address)
{
    unsigned char bytes[16] {};
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), bytes, sizeof(bytes), &count) ||
        count == 0)
        return "unreadable";

    std::string text;
    for (SIZE_T i = 0; i < count; ++i)
        text += std::format("{}{:02X}", i == 0 ? "" : " ", bytes[i]);
    return text;
}


void log_registers(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ContextRecord)
        return;
    const CONTEXT* context = info->ContextRecord;
    logger::error("FATAL: rip=0x{:016X} rsp=0x{:016X} rbp=0x{:016X}", context->Rip, context->Rsp, context->Rbp);
    logger::error("FATAL: instruction bytes {}", format_instruction_bytes(context->Rip));
}


// SetUnhandledExceptionFilter is not called for a stack overflow: there is no
// stack left to run it on. A vectored handler runs earlier and can still write
// a line, provided a little stack was reserved up front.
LONG CALLBACK on_stack_overflow(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord && info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        logger::error("FATAL: stack overflow on thread {}", GetCurrentThreadId());
        logger::flush();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}


std::string narrow(const wchar_t* text)
{
    if (!text)
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    out.pop_back();
    return out;
}


void on_pure_call()
{
    logger::error("FATAL: pure virtual function call");
    logger::flush();
}


void on_invalid_parameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line,
                          uintptr_t /*reserved*/)
{
    logger::error("FATAL: invalid parameter {} in {} at {}:{}", narrow(expression), narrow(function), narrow(file), line);
    logger::flush();
}

// The stack at the point the filter was entered, which still contains the
// frames that faulted.
void log_stack_trace()
{
    void* frames[64] {};
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
        logger::error("  #{:<2} {}", i, describe_address(reinterpret_cast<DWORD64>(frames[i])));
}


// Saves the state of the process to a file that Visual Studio can open. The
// file holds the call stack of every thread, so the line that crashed can be
// found without reproducing it under a debugger.
std::string write_crash_dump(EXCEPTION_POINTERS* info)
{
    const std::filesystem::path directory = std::filesystem::path(logger::log_file_path()).parent_path();

    SYSTEMTIME now {};
    GetLocalTime(&now);
    const std::string name = std::format("MusicReader-crash-{:04}{:02}{:02}-{:02}{:02}{:02}.dmp", now.wYear,
                                         now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::filesystem::path path = directory.empty() ? std::filesystem::path(name) : directory / name;

    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    MINIDUMP_EXCEPTION_INFORMATION exception_info {};
    exception_info.ThreadId = GetCurrentThreadId();
    exception_info.ExceptionPointers = info;
    exception_info.ClientPointers = FALSE;

    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithDataSegs);
    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                           info ? &exception_info : nullptr, nullptr, nullptr);
    CloseHandle(file);

    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return {};
    }
    return path.string();
}


// Runs while the process is dying: the logger still works, so this is the only
// chance to say what killed it.
LONG WINAPI crash_filter(EXCEPTION_POINTERS* info)
{
    const DWORD code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
    const auto address = (info && info->ExceptionRecord)
                             ? reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress)
                             : 0;

    // Symbols: the pdb sits beside the exe in the build output directory.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    const bool have_symbols = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;

    logger::error("FATAL: unhandled exception 0x{:08X} at {} on thread {}", code,
                  have_symbols ? describe_address(address) : std::format("0x{:X}", address),
                  GetCurrentThreadId());

    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR operation = info->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR target = info->ExceptionRecord->ExceptionInformation[1];
        logger::error("FATAL: {} of address 0x{:X}",
                      operation == 0 ? "read" : (operation == 1 ? "write" : "execute"), target);
    }

    log_registers(info);

    if (have_symbols)
        log_stack_trace();

    const std::string dump = write_crash_dump(info);
    if (dump.empty())
        logger::error("FATAL: crash dump could not be written, GetLastError={}", GetLastError());
    else
        logger::error("FATAL: crash dump written to {}", dump);

    logger::flush();

    // Let Windows do whatever it is configured to do (crash dialog, WER, dump).
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif


void log_terminate()
{
    std::string message = "FATAL: std::terminate";
    if (auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& ex) {
            message += std::format(": {}", ex.what());
        } catch (...) {
            message += ": unknown exception";
        }
    }
    logger::error(message);
    logger::flush();
    std::abort();
}

} // namespace


static std::string get_persistent_config_path(const std::string& file_name,
                                              [[maybe_unused]] const std::string& appname = "MusicReader")
{

    return file_name;
    /*
    // Determine the operating system
    std::filesystem::path config_path;

#if defined(_WIN32)
    const char *appdata = std::getenv("APPDATA");
    if (!appdata) throw std::runtime_error("APPDATA environment variable not set");
    config_path = std::filesystem::path(appdata) / appname;
#elif defined(__linux__)
    const char *home = std::getenv("HOME");
    if (!home) throw std::runtime_error("HOME environment variable not set");
    config_path = std::filesystem::path(home) / ".config" / appname;
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    if (!home) throw std::runtime_error("HOME environment variable not set");
    config_path = std::filesystem::path(home) / "Library" / "Application Support" / appname;
#else
    throw std::runtime_error("Unsupported OS");
#endif

    // Ensure the directory exists
    std::filesystem::create_directories(config_path);

    // Return the full path to the config file
    return (config_path / file_name).string();
    */
}


void set_high_precision(std::shared_ptr<spdlog::logger> logger, bool high_precision, bool date)
{
    if (!logger)
        return;
    if (high_precision) {
        if (date)
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] %v"); // microseconds!
        else
            logger->set_pattern("[%H:%M:%S.%f] [%l] %v"); // microseconds!
    } else {
        if (date)
            logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v"); // seconds
        else
            logger->set_pattern("[%H:%M:%S] [%l] %v"); // seconds
    }
}


void logger::configure_logger(const std::string& filename, size_t max_size_kb, bool log_to_console)
{
    try {
        std::vector<spdlog::sink_ptr> sinks;

        // Create a rotating file sink (max 1 backup file)
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, max_size_kb * 1024, 1));

        // Optionally log to console
        if (log_to_console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        logger_ = std::make_shared<spdlog::logger>("logger", sinks.begin(), sinks.end());
        spdlog::register_logger(logger_);
        logger_->set_level(spdlog::level::info);
        set_high_precision(logger_, false, false);
        // A crash loses whatever is still buffered, so flush often enough that
        // the lines leading up to it survive.
        spdlog::flush_every(std::chrono::seconds(1));
    } catch (const std::exception& ex) {
        std::cerr << "Failed to initialize logger: " << ex.what() << std::endl;
    }
}


void logger::install_crash_handler()
{
#ifdef _WIN32
    // Reserve a little stack so a stack overflow can still be logged.
    ULONG guaranteed = 64 * 1024;
    SetThreadStackGuarantee(&guaranteed);

    // Handles the exceptions Windows kills the process for before the filter runs.
    AddVectoredExceptionHandler(1, on_stack_overflow);
    SetUnhandledExceptionFilter(crash_filter);

    _set_purecall_handler(on_pure_call);
    _set_invalid_parameter_handler(on_invalid_parameter);
#endif
    std::set_terminate(log_terminate);
}


void logger::initialize(bool log_to_console, ConfigFile& cf, size_t max_size_kb)
{
    SAFE_METHOD;

    log_file_path_ = get_persistent_config_path("MusicReader.log");
    std::cout << "opening log file: " << log_file_path_ << std::endl;

    if (!cf.append_to_log()) {
        try {
            std::filesystem::remove(log_file_path_);
        } catch (const std::exception&) {
            ; // ignore, this just means the file doesn't exist
        }
    }

    configure_logger(log_file_path_, max_size_kb, log_to_console);
    logged_error_ = false;
    config_file_ = &cf;

    // Marks the start of a run, so a log that holds several runs can be read.
#ifdef _WIN32
    info("=== session start: pid={} exe={} ===", GetCurrentProcessId(), process_executable_path());
#else
    info("=== session start ===");
#endif
}


void logger::shutdown()
{
    if (logger_) {
        // Only a clean exit writes this line; a crash does not.
        logger_->info("=== session end ===");
        logger_->flush();
        spdlog::shutdown();
        logger_ = nullptr;
        logged_error_ = false;
    }
}


std::string logger::log_file_path()
{
    return log_file_path_;
}


bool logger::logged_error()
{
    return logged_error_;
}


void logger::flush()
{
    if (logger_)
        logger_->flush();
}


void logger::info(const std::string& message)
{
    if (logger_)
        logger_->info(message);
}


void logger::info(const std::u8string& message)
{
    std::string s {reinterpret_cast<const char*>(message.data()), message.size()};
    info(s);
}


void logger::warning(const std::string& message)
{
    if (logger_)
        logger_->warn(message);
}


void logger::warning(const std::u8string& message)
{
    std::string s {reinterpret_cast<const char*>(message.data()), message.size()};
    warning(s);
}

void logger::error(const std::string& message)
{
    if (logger_) {
        logged_error_ = true;
        logger_->error(message);
    }
}


void logger::error(const std::u8string& message)
{
    std::string s {reinterpret_cast<const char*>(message.data()), message.size()};
    error(s);
}


void logger::debug(const std::string& message)
{
    if (!logger_)
        return;
    auto level = config_file_->log_level();
    if (level == LogLevel::Diagnostic || level == LogLevel::DebugAndTrace)
        logger_->debug(message);
}


void logger::debug(const std::u8string& message)
{
    std::string s {reinterpret_cast<const char*>(message.data()), message.size()};
    debug(s);
}


void logger::trace(const std::string& message)
{
    if (!logger_)
        return;

    auto level = config_file_->log_level();
    bool log = false;

    if (ConfigFile::instance().trace_while_debug_logging())
        log = (level == LogLevel::Trace || level == LogLevel::Diagnostic || level == LogLevel::DebugAndTrace);
    else
        log = (level == LogLevel::Trace || level == LogLevel::DebugAndTrace);

    if (log)
        logger_->trace(message);
}


void logger::trace(const std::u8string& message)
{
    std::string s {reinterpret_cast<const char*>(message.data()), message.size()};
    trace(s);
}


void logger::enable_debug_logging(bool enable)
{
    if (!logger_)
        return;

    auto level = spdlog::level::info;
    bool allow_trace = false;

    if (enable) {
        level = spdlog::level::debug;

        if (ConfigFile::instance().trace_while_debug_logging()) {
            allow_trace = true;
            level = spdlog::level::trace;
        }
    }

    logger_->set_level(level);
    for (auto& sink : logger_->sinks())
        sink->set_level(level);

    trace_enabled_ = allow_trace;
    debug_enabled_ = enable;
    set_high_precision(logger_, false, false);
}


void logger::enable_debug_and_trace_logging(bool enable)
{
    if (!logger_)
        return;

    auto level = enable ? spdlog::level::trace : spdlog::level::info;
    logger_->set_level(level);
    for (auto& sink : logger_->sinks())
        sink->set_level(level);

    trace_enabled_ = enable;
    debug_enabled_ = enable;
    set_high_precision(logger_, true, false);
}


void logger::enable_trace_logging(bool enable)
{
    if (!logger_)
        return;
    logger_->set_level(enable ? spdlog::level::trace : spdlog::level::info);
    for (auto& sink : logger_->sinks())
        sink->set_level(enable ? spdlog::level::trace : spdlog::level::info);

    trace_enabled_ = true;
    debug_enabled_ = false;
    set_high_precision(logger_, true, false);

    /* we are logging with debug right now, not sure why
    if (enable)
        logger_->flush_on(spdlog::level::trace);
    else
        logger_->flush_on(spdlog::level::debug);
        */
}


std::string logger::get_log_content()
{
    if (!logger_)
        return {};

    logger_->flush();

    auto sinks = logger_->sinks();
    for (const auto& sink : sinks) {
        auto file_sink = std::dynamic_pointer_cast<spdlog::sinks::rotating_file_sink_mt>(sink);
        if (file_sink) {
            std::string log_path = file_sink->filename();
            std::ifstream file(log_path);
            if (!file.is_open()) {
                return "";
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    return "";
}

std::unordered_set<std::string> func_names;
void test_strip_extra_call_info()
{
    struct TestCase {
        std::string input;
        std::string expected;
    };

    std::vector<TestCase> tests = {
        {                                                       "void __cdecl MusicReader::setup_UI(void)","setup_UI()"                                                                                                           },
        {                                          "void __cdecl MusicReader::create_bookmark_panel(void)",     "create_bookmark_panel()"},
        {                   "void __cdecl MusicReader::create_file_menu<class QMenuBar>(class QMenuBar *)",          "create_file_menu()"},
        {                   "void __cdecl MusicReader::create_edit_menu<class QMenuBar>(class QMenuBar *)",          "create_edit_menu()"},
        {                  "void __cdecl MusicReader::create_imslp_menu<class QMenuBar>(class QMenuBar *)",         "create_imslp_menu()"},
        {                   "void __cdecl MusicReader::create_view_menu<class QMenuBar>(class QMenuBar *)",          "create_view_menu()"},
        {                   "void __cdecl MusicReader::create_help_menu<class QMenuBar>(class QMenuBar *)",          "create_help_menu()"},
        {                                      "class QIcon __cdecl MusicReader::create_double_icon(void)",        "create_double_icon()"},
        {                                      "bool __cdecl MusicReader::in_single_page_mode(void) const",       "in_single_page_mode()"},
        {                                                          "class std::shared_ptr<class Document> __cdecl MusicReader::open_pdf_document(const class "
                                                          "std::filesystem::path &,int)",         "open_pdf_document()"         },
        {         "class std::vector<class Annotation,class std::allocator<class Annotation> > __cdecl "
         "Document::load_annotations_from_pdf(struct fz_context *,struct fz_document *)", "load_annotations_from_pdf()"         },
        {                           "enum Qt::AlignmentFlag __cdecl PDFViewer::page_alignment(void) const",            "page_alignment()"},
        {      "struct PDFViewer::PrefetchEntry __cdecl PDFViewer::make_single_page_entry(int,bool) const",
         "make_single_page_entry()"                                                                                                      },
        {                                         "struct Page __cdecl Document::get_page(int,bool) const",                  "get_page()"},
        {                                    "void __cdecl PDFViewer::update_image(const class QString &)",              "update_image()"},
        {                                                           "void __cdecl PDFViewer::on_page_loaded(class std::basic_string<char,struct std::char_traits<char>,class "
                                                           "std::allocator<char> >,int)",            "on_page_loaded()"         },
        {              "auto __cdecl MusicReader::setup_mouse_hiding::<lambda_1>::operator ()(void) const",
         "setup_mouse_hiding::<lambda_1>::operator ()"                                                                                   },
        {         "class std::vector<int,class std::allocator<int> > __cdecl get_page_load_order(int,int)",
         "get_page_load_order()"                                                                                                         },
        {"void __cdecl DocumentLoadManager::prioritize_page_internal(const class std::filesystem::path &)",
         "prioritize_page_internal()"                                                                                                    },
    };

    int passed = 0;
    int failed = 0;
    std::ofstream f;
    f.open("c:\\tmp\\funcsigtest.txt");

    for (const auto& test : tests) {
        std::string result = strip_extra_call_info(test.input, false);
        if (result == test.expected) {
            passed++;
        } else {
            failed++;
            f << "FAIL: " << test.input << "\n"
              << "  Expected: " << test.expected << "\n"
              << "  Got:      " << result << "\n";
        }
    }

    f << "\nTests: " << passed << " passed, " << failed << " failed\n";
}

std::string strip_extra_call_info(const std::string& funcsig, bool log)
{
    std::ofstream f;

    if (log)
        log = !func_names.contains(funcsig);

    if (log) {
        func_names.insert(funcsig);
        f.open("c:\\tmp\\funcsig.txt", std::ios::app);
        f << funcsig << " ";
    }

    if (funcsig.find("<lambda_") != std::string::npos) {
        size_t lambda_start = funcsig.find("::");
        if (lambda_start != std::string::npos) {
            lambda_start += 2;
            size_t paren_pos = funcsig.find('(', lambda_start);
            if (paren_pos != std::string::npos) {
                std::string lambda_part = funcsig.substr(lambda_start, paren_pos - lambda_start) + "()";
                if (log)
                    f << lambda_part << std::endl;
                return lambda_part;
            }
        }
    }

    size_t paren_pos = funcsig.find('(');
    if (paren_pos == std::string::npos) {
        if (log)
            f << std::endl;
        return funcsig;
    }

    std::string before_paren = funcsig.substr(0, paren_pos);

    // Find rightmost space that's not inside angle brackets
    size_t func_start = std::string::npos;
    int angle_depth = 0;

    for (size_t i = before_paren.length(); i-- > 0;) {
        if (before_paren[i] == '>')
            angle_depth++;
        else if (before_paren[i] == '<')
            angle_depth--;
        else if (angle_depth == 0 && before_paren[i] == ' ') {
            func_start = i + 1;
            break;
        }
    }

    if (func_start == std::string::npos)
        func_start = 0;

    std::string func_name = before_paren.substr(func_start);

    // Remove template parameters
    size_t template_start = func_name.find('<');
    if (template_start != std::string::npos)
        func_name.resize(template_start);

    // Remove class scope prefix (Class::)
    size_t scope_op = func_name.rfind("::");
    if (scope_op != std::string::npos)
        func_name = func_name.substr(scope_op + 2);

    func_name += "()";
    if (log)
        f << func_name << std::endl;
    return func_name;
}

std::atomic<int> indent_level_;

int indent_level()
{
    return indent_level_;
}


void increase_indent()
{
    indent_level_ += 2;
}


void decrease_indent()
{
    if (indent_level_ > 0)
        indent_level_ -= 2;
}

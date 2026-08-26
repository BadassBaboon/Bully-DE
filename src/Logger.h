#pragma once
#include <string>
#include <string_view>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <format>
#include <chrono>

namespace BullyDE {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& Get();

    void Initialize(const std::filesystem::path& logPath, LogLevel minLevel = LogLevel::Info, bool enableFile = true);
    void Shutdown();

    void Log(LogLevel level, std::string_view tag, std::string_view message);

    template <typename... Args>
    void Debug(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Debug, tag, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Info(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Info, tag, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Warn(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Warn, tag, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Error(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        Log(LogLevel::Error, tag, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream m_logFile;
    std::mutex m_mutex;
    LogLevel m_minLevel{ LogLevel::Info };
    bool m_enableFile{ true };
    bool m_initialized{ false };
};

} // namespace BullyDE

#include "Logger.h"
#include <iostream>
#include <Windows.h>

namespace BullyDE {

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Initialize(const std::filesystem::path& logPath, LogLevel minLevel, bool enableFile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = minLevel;
    m_enableFile = enableFile;

    if (m_enableFile) {
        if (m_logFile.is_open()) {
            m_logFile.close();
        }

        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);

        m_logFile.open(logPath, std::ios::out | std::ios::trunc);
        if (m_logFile.is_open()) {
            m_initialized = true;
            auto now = std::chrono::system_clock::now();
            auto timeStr = std::format("{:%Y-%m-%d %H:%M:%S}", now);
            m_logFile << "====================================================\n";
            m_logFile << " Bully: Definitive Edition (Bully-DE.asi) Log\n";
            m_logFile << " Started: " << timeStr << "\n";
            m_logFile << "====================================================\n" << std::flush;
        }
    } else {
        m_initialized = true;
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) {
        m_logFile << "====================================================\n";
        m_logFile << " Bully: Definitive Edition Session Ended.\n";
        m_logFile << "====================================================\n" << std::flush;
        m_logFile.close();
    }
    m_initialized = false;
}

void Logger::Log(LogLevel level, std::string_view tag, std::string_view message) {
    if (level < m_minLevel) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const char* levelStr = "INFO";
    switch (level) {
    case LogLevel::Debug: levelStr = "DEBUG"; break;
    case LogLevel::Info:  levelStr = "INFO";  break;
    case LogLevel::Warn:  levelStr = "WARN";  break;
    case LogLevel::Error: levelStr = "ERROR"; break;
    }

    auto now = std::chrono::system_clock::now();
    std::string formatted = std::format("[{:%H:%M:%S}] [{}] [{}] {}\n", now, levelStr, tag, message);

    // Output to OutputDebugString for debugger tools
    OutputDebugStringA(formatted.c_str());

    if (m_enableFile && m_logFile.is_open()) {
        m_logFile << formatted << std::flush;
    }
}

} // namespace BullyDE

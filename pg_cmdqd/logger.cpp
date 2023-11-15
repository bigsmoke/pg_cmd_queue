/*
Shamelessly copied/adapted from FlashMQ (https://www.flashmq.org)
*/

#include "logger.h"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <string.h>
#include <functional>

#include <errno.h>

#include "utils.h"

std::string normalize_log_level(const std::string &str)
{
    if (str.substr(0, 4) == "LOG_")
        return str.substr(4);
    return str;
}

StreamToLog::StreamToLog(LogLevel level) :
    level(level)
{

}

StreamToLog::~StreamToLog()
{
    const std::string s = str();
    Logger *logger = Logger::getInstance();
    logger->log(this->level, s.c_str());
}

LogLine::LogLine(std::string &&line, bool alsoToStdOut) :
    line(line),
    alsoToStdOut(alsoToStdOut)
{

}

LogLine::LogLine(const char *s, size_t len, bool alsoToStdOut) :
    line(s, len),
    alsoToStdOut(alsoToStdOut)
{

}

LogLine::LogLine() :
    alsoToStdOut(true)
{

}

const char *LogLine::c_str() const
{
    return line.c_str();
}

bool LogLine::alsoLogToStdOut() const
{
    return alsoToStdOut;
}

Logger::Logger()
{
    memset(&linesPending, 1, sizeof(sem_t));
    sem_init(&linesPending, 0, 0);

    auto f = std::bind(&Logger::writeLog, this);
    this->writerThread = std::thread(f, this);

#ifdef __USE_GNU
    pthread_t native = this->writerThread.native_handle();
    pthread_setname_np(native, "LogWriter");
#endif
}

Logger::~Logger()
{
    if (running)
        quit();

    if (file)
    {
        fclose(file);
        file = nullptr;
    }

    sem_close(&linesPending);
}

std::string Logger::getLogLevelString(LogLevel level) const
{
    switch (level)
    {
    case LOG_NONE:
        return "NONE";
    case LOG_PANIC:
        return "PANIC";
    case LOG_FATAL:
        return "FATAL";
    case LOG_LOG:
        return "LOG";
    case LOG_ERROR:
        return "ERROR";
    case LOG_WARNING:
        return "WARNING";
    case LOG_NOTICE:
        return "NOTICE";
    case LOG_INFO:
        return "INFO";
    case LOG_DEBUG1:
        return "DEBUG1";
    case LOG_DEBUG2:
        return "DEBUG2";
    case LOG_DEBUG3:
        return "DEBUG3";
    case LOG_DEBUG4:
        return "DEBUG4";
    case LOG_DEBUG5:
        return "DEBUG5";
    default:
        return "UNKNOWN LOG LEVEL";
    }
}

thread_local std::shared_ptr<CmdQueue> Logger::cmd_queue = nullptr;

Logger *Logger::getInstance()
{
    static Logger instance;
    return &instance;
}

void Logger::queueReOpen()
{
    reload = true;
    sem_post(&linesPending);
}

void Logger::reOpen()
{
    reload = false;

    if (file)
    {
        fclose(file);
        file = nullptr;
    }

    if (logPath.empty())
        return;

    if ((file = fopen(logPath.c_str(), "a")) == nullptr)
    {
        log(LOG_ERROR, "(Re)opening log file '%s' error: %s. Logging to stdout.", logPath.c_str(), strerror(errno));
    }
}

// I want all messages logged during app startup to also show on stdout/err, otherwise failure can look so silent. So, call this when the app started.
void Logger::noLongerLogToStd()
{
    if (!logPath.empty())
        log(LOG_INFO, "Switching logging from stdout to logfile '%s'", logPath.c_str());
    alsoLogToStd = false;
}

void Logger::setLogPath(const std::string &path)
{
    this->logPath = path;
}

void Logger::setLogLevel(LogLevel level)
{
    curLogLevel = level;
}

void Logger::quit()
{
    running = false;
    sem_post(&linesPending);
    if (writerThread.joinable())
        writerThread.join();
}

void Logger::writeLog()
{
    maskAllSignalsCurrentThread();

    int graceCounter = 0;

    LogLine line;
    while(running || (!lines.empty() && graceCounter++ < 1000 ))
    {
        sem_wait(&linesPending);

        if (reload)
        {
            reOpen();
        }

        {
            std::lock_guard<std::mutex> locker(logMutex);

            if (lines.empty())
                continue;

            line = std::move(lines.front());
            lines.pop();
        }

        if (this->file)
        {
            if (fputs(line.c_str(), this->file) < 0 ||
                fputs("\n", this->file) < 0 ||
                fflush(this->file) != 0)
            {
                alsoLogToStd = true;
                fputs("Writing to log failed. Enabling stdout logger.", stderr);
            }
        }

        if (!this->file || line.alsoLogToStdOut())
        {
            FILE *output = stdout;
#ifdef TESTING
            output = stderr; // the stdout interfers with Qt test XML output, so using stderr.
#endif
            fputs(line.c_str(), output);
            fputs("\n", output);
            fflush(output);
        }
    }
}

void Logger::log(LogLevel level, const char *str, va_list valist)
{
    if (level > curLogLevel)
        return;

    time_t time = std::time(nullptr);
    struct tm tm = *std::localtime(&time);
    std::ostringstream oss;

    std::string str_(str);
    if (cmd_queue != nullptr) oss << "\x1b" << cmd_queue->ansi_fg;
    if (logTimes) oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ";
    if (cmd_queue != nullptr) oss << "[" << cmd_queue->cmd_class_identity << "] ";
    oss << "[" << getLogLevelString(level) << "] " << str_;
    if (cmd_queue != nullptr) oss << "\x1b[0m";
    oss.flush();
    const std::string s = oss.str();
    const char *logfmtstring = s.c_str();

    va_list valisttmp;
    va_copy(valisttmp, valist);
    const int buf_size = vsnprintf(nullptr, 0, logfmtstring, valisttmp) + 1;
    va_end(valisttmp);

    char buf[buf_size + 1];
    buf[buf_size] = 0;

    va_list valist2;
    va_copy(valist2, valist);
    vsnprintf(buf, buf_size, logfmtstring, valist2);
    size_t len = std::min<size_t>(buf_size, strlen(buf));
    LogLine line(buf, len, alsoLogToStd);
    va_end(valist2);

    {
        std::lock_guard<std::mutex> locker(logMutex);
        lines.push(std::move(line));
    }

    sem_post(&linesPending);
}

void Logger::log(LogLevel level, const char *str, ...)
{
    va_list valist;
    va_start(valist, str);
    this->log(level, str, valist);
    va_end(valist);
}

/**
 * @brief Logger::log
 * @param level
 * @return a StreamToLog, that you're not suppose to name. When you don't, its destructor will log the stream.
 *
 * Allows logging like: logger->log(LOG_NOTICE) << "blabla: " << 1 << ".". The advantage is safety (printf crashes), and not forgetting printf arguments.
 *
 * Beware though: C++ streams chars as characters. When you have an uint8_t or int8_t that's also a char, and those need to be cast to int first. A good
 * solution needs to be devised.
 */
StreamToLog Logger::logstream(LogLevel level)
{
    return StreamToLog(level);
}

/*
void Logger::log(LogLevel level, const CmdQueue &cmd_queue, const char *str, va_list valist)
{
    // TODO: DRY this method
    if (level > curLogLevel)
        return;

    time_t time = std::time(nullptr);
    struct tm tm = *std::localtime(&time);
    std::ostringstream oss;

    std::string str_(str);
    oss << "\x1b" << cmd_queue.ansi_fg << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ["
        << cmd_queue.cmd_class_identity << "] [" << getLogLevelString(level) << "] " << str_ << "\x1b[0m";
    oss.flush();
    const std::string s = oss.str();
    const char *logfmtstring = s.c_str();

    constexpr const int buf_size = 512;

    char buf[buf_size + 1];
    buf[buf_size] = 0;

    va_list valist2;
    va_copy(valist2, valist);
    vsnprintf(buf, buf_size, logfmtstring, valist);
    size_t len = std::min<size_t>(buf_size, strlen(buf));
    LogLine line(buf, len, alsoLogToStd);
    va_end(valist2);

    {
        std::lock_guard<std::mutex> locker(logMutex);
        lines.push(std::move(line));
    }

    sem_post(&linesPending);
}

void Logger::log(LogLevel level, const CmdQueue &cmd_queue, const char *str, ...)
{
    va_list valist;
    va_start(valist, str);
    this->log(level, cmd_queue, str, valist);
    va_end(valist);
}
*/

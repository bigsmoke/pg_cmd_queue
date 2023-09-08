#ifndef LOGGER_H
#define LOGGER_H

#include <semaphore.h>
#include <stdio.h>
#include <stdarg.h>

#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <map>

#include "cmdqueue.h"

/**
 * The log levels and their interpretations have been taken from PostgreSQL.
 * See “Message Severity Levels” in the Postgres documentation for details:
 * https://www.postgresql.org/docs/current/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS
 */
enum LogLevel {
    // I am unter, for I cannot read hexidecimal. ¯\_(ツ)_/¯
    LOG_NONE    = 0b00000000000000000000000000000000,
    LOG_PANIC   = 0b00000000000000000000000000000001,       // LOG_CRIT    in syslog
    LOG_FATAL   = 0b00000000000000000000000000000011,       // LOG_ERR     in syslog
    LOG_LOG     = 0b00000000000000000000000000000111,       // LOG_INFO    in syslog
    LOG_ERROR   = 0b00000000000000000000000000001111,       // LOG_WARNING in syslog
    LOG_WARNING = 0b00000000000000000000000000011111,       // LOG_NOTICE  in syslog
    LOG_NOTICE  = 0b00000000000000000000000000111111,       // LOG_NOTICE  in syslog
    LOG_INFO    = 0b00000000000000000000000001111111,       // LOG_INFO    in syslog
    LOG_DEBUG1  = 0b00000000000000000000000011111111,       // LOG_DEBUG   in syslog
    LOG_DEBUG2  = 0b00000000000000000000000111111111,       // LOG_DEBUG   in syslog
    LOG_DEBUG3  = 0b00000000000000000000001111111111,       // LOG_DEBUG   in syslog
    LOG_DEBUG4  = 0b00000000000000000000011111111111,       // LOG_DEBUG   in syslog
    LOG_DEBUG5  = 0b00000000000000000000111111111111,       // LOG_DEBUG   in syslog
};

const std::map<std::string, LogLevel> StringToLogLevel = {
    {"LOG_NONE",    LOG_NONE},
    {"LOG_PANIC",   LOG_PANIC},
    {"LOG_FATAL",   LOG_FATAL},
    {"LOG_LOG",     LOG_LOG},
    {"LOG_ERROR",   LOG_ERROR},
    {"LOG_WARNING", LOG_WARNING},
    {"LOG_NOTICE",  LOG_NOTICE},
    {"LOG_INFO",    LOG_INFO},
    {"LOG_DEBUG1",  LOG_DEBUG1},
    {"LOG_DEBUG2",  LOG_DEBUG2},
    {"LOG_DEBUG3",  LOG_DEBUG3},
    {"LOG_DEBUG4",  LOG_DEBUG4},
    {"LOG_DEBUG5",  LOG_DEBUG5},
};


class LogLine
{
    std::string line;
    bool alsoToStdOut;

public:
    LogLine(std::string &&line, bool alsoToStdOut);
    LogLine(const char *s, size_t len, bool alsoToStdOut);
    LogLine();
    LogLine(const LogLine &other) = delete;
    LogLine(LogLine &&other) = default;
    LogLine &operator=(LogLine &&other) = default;

    const char *c_str() const;
    bool alsoLogToStdOut() const;
};

/**
 * The `Logger` class had been adapted from the `Logger` class created by Wiebe Cazemier for the
 * FOSS FlashMQ software (https://www.flashmq.org).
 */
class Logger
{
    std::string logPath;
    LogLevel curLogLevel = LOG_NOTICE;
    std::mutex logMutex;
    std::queue<LogLine> lines;
    sem_t linesPending;
    std::thread writerThread;
    bool running = true;
    FILE *file = nullptr;
    bool alsoLogToStd = true;
    bool reload = false;

    Logger();
    ~Logger();
    std::string getLogLevelString(LogLevel level) const;
    void reOpen();
    void writeLog();

public:
    thread_local static std::shared_ptr<CmdQueue> cmd_queue;

    static Logger *getInstance();
    void log(LogLevel level, const char *str, va_list args);
    void log(LogLevel level, const char *str, ...);
    //void log(LogLevel level, const CmdQueue &cmd_queue, const char *str, va_list args);
    //void log(LogLevel level, const CmdQueue &cmd_queue, const char *str, ...);

    void queueReOpen();
    void noLongerLogToStd();

    void setLogPath(const std::string &path);
    void setLogLevel(const LogLevel level);

    void quit();

};

#endif // LOGGER_H

#ifndef HSLL_SPLOG
#define HSLL_SPLOG

#include <iostream>

/**
 * @brief Macro for logging information with specified level
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO(level, ...) \
    LogInfo(true, level, __VA_ARGS__);

/**
 * @brief Macro for conditional logging without prefix
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_NOPREFIX(level, ...)   \
    {                                       \
        LogInfo(false, level, __VA_ARGS__); \
    }

namespace HSLL
{
    /**
     * @brief Enumeration for log levels
     */
    enum LOG_LEVEL
    {
        LOG_LEVEL_INFO = 0,    // Informational messages
        LOG_LEVEL_WARNING = 1, // Warning messages
        LOG_LEVEL_CRUCIAL = 2, // Crucial messages
        LOG_LEVEL_ERROR = 3,   // Error messages
    };

#define HSLL_MIN_LOG_LEVEL LOG_LEVEL_WARNING

    /**
     * @brief Utility method for logging information
     * @tparam TS Variadic template parameters
     * @param level Log level to use
     * @param ts Variadic arguments to log
     * @note Uses variadic templates to support multiple arguments.
     *       A thread-safe logging library is recommended instead.
     */
    template <class... TS>
    static void LogInfo(bool prefix, LOG_LEVEL level, TS... ts)
    {

#if defined(NOLOG_) || defined(_NOLOG)
        return;
#else
        constexpr const char *const LevelStr[] = {
            "\033[92m[INFO]\033[0m ",
            "\033[93m[WARNING]\033[0m ",
            "\033[95m[CRUCIAL]\033[0m ",
            "\033[91m[ERROR]\033[0m "};

#if !defined(_DEBUG) && !defined(DEBUG_)
        if (level >= HSLL_MIN_LOG_LEVEL)
        {
#endif//DEBUG

            if (sizeof...(TS))
            {
                if (prefix)
                    (std::cout << LevelStr[level]<< ... << ts) << std::endl;
                else
                    (std::cout << ... << ts) << std::endl;
            }

#if !defined(_DEBUG) && !defined(DEBUG_)
        }
#endif//DEBUG

#endif//NOLOG
    }
}

#endif//HSLL_SPLOG

#ifndef HSLL_LOG
#define HSLL_LOG

#include <iostream>

/**
 * @brief Macro for logging information with specified level
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO(level, ...) \
    LogInfo(true, level, __VA_ARGS__);

/**
 * @brief Macro for logging information with specified level and function call
 * @param level Log level to use
 * @param func Function to call after logging
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_FUNC(level, func, ...) \
    {                                       \
        LogInfo(true, level, __VA_ARGS__);  \
        func;                               \
    }

/**
 * @brief Macro for conditional logging
 * @param level Log level to use
 * @param exp Expression to evaluate
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_EXP(level, exp, ...)      \
    {                                          \
        if (exp)                               \
            LogInfo(true, level, __VA_ARGS__); \
    }

/**
 * @brief Macro for conditional logging with additional function call
 * @param level Log level to use
 * @param exp Expression to evaluate
 * @param func Function to call if expression is true
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_EXP_FUNC(level, exp, func, ...) \
    {                                                \
        if (exp)                                     \
        {                                            \
            LogInfo(true, level, __VA_ARGS__);       \
            func;                                    \
        }                                            \
    }

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
#ifdef _WIN32
        constexpr const char *const LevelStr[] = {
            "[INFO] ",
            "[WARNING] ",
            "[CRUCIAL] ",
            "[ERROR] "};
#else
        constexpr const char *const LevelStr[] = {
            "\033[92m[INFO]\033[0m ",
            "\033[93m[WARNING]\033[0m ",
            "\033[95m[CRUCIAL]\033[0m ",
            "\033[91m[ERROR]\033[0m "};
#endif

#if defined(_NOINFO) || defined(_NOLOG)
        return;
#endif

#if !defined(_DEBUG) && !defined(DEBUG_)
        if (level >= HSLL_MIN_LOG_LEVEL)
        {
#endif
            if (sizeof...(TS))
            {
                if (prefix)
                    (std::cout << LevelStr[level] << " " << ... << ts) << std::endl;
                else
                    (std::cout << ... << ts) << std::endl;
            }
#if !defined(_DEBUG) && !defined(DEBUG_)
        }
#endif
    }
}

#endif

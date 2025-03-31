#ifndef HSLL_LOG
#define HSLL_LOG

#include <iostream>

/**
 * @brief Macro for logging information with specified level
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO(level, ...) \
    LogInfo(level, __VA_ARGS__);

/**
 * @brief Macro for logging information with specified level and function call
 * @param level Log level to use
 * @param func Function to call after logging
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_FUNC(level, func, ...) \
    {                                       \
        LogInfo(level, __VA_ARGS__);        \
        func;                               \
    }

/**
 * @brief Macro for conditional logging
 * @param level Log level to use
 * @param exp Expression to evaluate
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_EXP(level, exp, ...) \
    {                                     \
        if (exp)                          \
            LogInfo(level, __VA_ARGS__);  \
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
            LogInfo(level, __VA_ARGS__);             \
            func;                                    \
        }                                            \
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

    /**
     * @brief Utility method for logging information
     * @tparam TS Variadic template parameters
     * @param level Log level to use
     * @param ts Variadic arguments to log
     * @note Uses variadic templates to support multiple arguments.
     *       A thread-safe logging library is recommended instead.
     */
    template <class... TS>
    static void LogInfo(LOG_LEVEL level, TS... ts)
    {
        constexpr const char *const LevelStr[] = {
            "[INFO]",
            "[WARNING]",
            "[CRUCIAL]",
            "[ERROR]"};

#if defined(_NOINFO) || defined(_NOINFO)

#elif defined(_DEBUG) || defined(DEBUG_)
        if (sizeof...(TS))
            (std::cout << LevelStr[level] << " " << ... << ts) << std::endl;
#else
        if (level > LOG_LEVEL_INFO)
        {
            if (sizeof...(TS))
                (std::cout << LevelStr[level] << " " << ... << ts) << std::endl;
        }
#endif
    }
}

#endif

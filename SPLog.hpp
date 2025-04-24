#ifndef HSLL_SPLOG
#define HSLL_SPLOG

#include <iostream>

#include "SPTypes.h"

using namespace HSLL::DEFER;

/**
 * @brief Macro for logging information with specified level
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO(level, ...)                \
    {                                           \
        if (level >= configGlobal.MIN_LOG_LEVEL) \
            LogInfo(true, level, __VA_ARGS__);  \
    }

/**
 * @brief Macro for conditional logging without prefix
 * @param level Log level to use
 * @param ... Variadic arguments to log
 */
#define HSLL_LOGINFO_NOPREFIX(level, ...)       \
    {                                           \
        if (level >= configGlobal.MIN_LOG_LEVEL) \
            LogInfo(false, level, __VA_ARGS__); \
    }

namespace HSLL
{
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
        constexpr const char *const LevelStr[] = {
            "\033[92m[INFO]\033[0m ",
            "\033[93m[WARNING]\033[0m ",
            "\033[95m[CRUCIAL]\033[0m ",
            "\033[91m[ERROR]\033[0m "};

        if (sizeof...(TS))
        {
            if (prefix)
                (std::cout << LevelStr[level] << ... << ts) << std::endl;
            else
                (std::cout << ... << ts) << std::endl;
        }
    }
}

#endif // HSLL_SPLOG
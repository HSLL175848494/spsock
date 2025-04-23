#ifndef HSLL_SPINITIALIZER
#define HSLL_SPINITIALIZER

#include <assert.h>

#include "SPTask.hpp"
#include "SPController.h"

namespace HSLL
{

    /**
     * @brief Provides delayed initialization for function pointers
     * @details This class serves as a mediator for function pointers that need to reference
     *          post-defined functions. It ensures these pointers are initialized after
     *          their target functions are defined, solving static initialization order issues.
     */
    class SPInitializer : noncopyable
    {
    public:
        /**
         * @brief Processes socket read operations and data handling
         * @param ctx Socket controller managing connection state and buffers
         * @param proc Callback for processing received data after successful read
         * @details This function:
         *          1. Attempts socket read via controller's readSocket()
         *          2. Disables events on read failure
         *          3. Invokes processing callback on success
         */
        static void TaskFunc(SOCKController *ctx, ReadWriteProc proc)
        {
            if (!ctx->readSocket())
                ctx->enableEvents(false, false);
            else
                proc(ctx);
        }

        /**
         * @brief Re-enables event monitoring for the controller
         * @param controller Socket controller to operate on
         * @details Attempts to re-enable stored events. Closes connection on failure.
         */
        static void REnableFunc(SOCKController *controller)
        {
            if (!controller->renableEvents())
                controller->close();
        }

        /**
         * @brief Initializes function pointers with concrete implementations
         * @return Always returns true to indicate successful initialization
         * @note This must be called after target function definitions exist
         */
        static void Init(SPConfig &config)
        {
            assert(config.READ_BSIZE > 0);
            assert(config.WRITE_BSIZE > 0);
            assert(config.MAX_EVENT_BSIZE > 0);
            assert(config.EPOLL_TIMEOUT_MILLISECONDS > 0 || config.EPOLL_TIMEOUT_MILLISECONDS == -1);
            assert(config.EPOLL_DEFAULT_EVENT == EPOLLIN || config.EPOLL_DEFAULT_EVENT == EPOLLOUT || config.EPOLL_DEFAULT_EVENT == (EPOLLIN | EPOLLOUT));
            assert(config.THREADPOOL_QUEUE_LENGTH > 0);
            assert(config.THREADPOOL_DEFAULT_THREADS_NUM > 0);
            assert(config.THREADPOOL_BATCH_SIZE_SUBMIT > 0);
            assert(config.THREADPOOL_BATCH_SIZE_PROCESS > 0);
            taskProc = TaskFunc;
            renableProc = REnableFunc;
            configGlobal = config;
        }
    };
}

#endif
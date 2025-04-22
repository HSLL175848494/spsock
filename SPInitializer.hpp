#ifndef HSLL_SPINITIALIZER
#define HSLL_SPINITIALIZER

#include "SPTask.hpp"
#include "SPController.hpp"

namespace HSLL
{
    
    /**
     * @brief Provides delayed initialization for function pointers
     * @details This class serves as a mediator for function pointers that need to reference
     *          post-defined functions. It ensures these pointers are initialized after
     *          their target functions are defined, solving static initialization order issues.
     */
    class SPInitializer
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
        static bool Init()
        {
            taskProc = TaskFunc;
            renableProc = REnableFunc;
            return true;
        }
    };

    /**
     * @brief Global initializer that triggers function pointer setup
     * @details Leverages static initialization to ensure Init() runs before main()
     */
    bool SPInitializer_Init = SPInitializer::Init();
}

#endif
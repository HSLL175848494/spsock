#ifndef HSLL_SPINITIALIZER
#define HSLL_SPINITIALIZER

#include "SPTask.hpp"
#include "SPController.h"

namespace HSLL::DEFER
{
    struct SPDefered
    {
        /**
         * @brief Processes socket read operations and data handling
         * @param ctx Socket controller managing connection state and buffers
         * @param proc Callback for processing received data after successful read
         * @details This function:
         *          1. Attempts socket read via controller's readSocket()
         *          2. Disables events on read failure
         *          3. Invokes processing callback on success
         */
        static void TaskFunc(SOCKController *ctx, ReadWriteProc proc);

        /**
         * @brief Re-enables event monitoring for the controller
         * @param controller Socket controller to operate on
         * @details Attempts to re-enable stored events. Closes connection on failure.
         */
        static void REnableFunc(SOCKController *controller);
    };

}

#endif
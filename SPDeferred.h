#ifndef HSLL_SPINITIALIZER
#define HSLL_SPINITIALIZER

#include "SPTask.hpp"
#include "SPController.h"

namespace HSLL::DEFER
{
    struct SPDefered
    {
        /**
         * @brief Re-enables event monitoring for the controller
         * @param controller Socket controller to operate on
         * @details Attempts to re-enable stored events. Closes connection on failure.
         */
        static void REnableFunc(SOCKController *controller);
    };

}

#endif
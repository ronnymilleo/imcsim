/**
 * @file    digital_models.h
 * @brief   Skips the tests that need the digital code models of ngspice when the ngspice in use has none.
 */

#ifndef IMCSIM_DIGITAL_MODELS_H
#define IMCSIM_DIGITAL_MODELS_H

#include "simulator.h"
#include <catch2/catch_test_macros.hpp>

// The Windows build of ngspice ships without digital.cm, so its tests run only where the library is found
#define SKIP_WITHOUT_DIGITAL_MODELS()                                                                                  \
    do {                                                                                                               \
        if (!Core::AreDigitalModelsAvailable()) {                                                                      \
            SKIP("This ngspice has no digital models (digital.cm)");                                                   \
        }                                                                                                              \
    } while (false)

#endif // IMCSIM_DIGITAL_MODELS_H

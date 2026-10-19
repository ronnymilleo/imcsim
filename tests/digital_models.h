/**
 * @file    digital_models.h
 * @brief   Skips the tests that need the digital code models of ngspice when the ngspice in use has none.
 */

#ifndef IMCSIM_DIGITAL_MODELS_H
#define IMCSIM_DIGITAL_MODELS_H

#include "simulator.h"
#include <catch2/catch_test_macros.hpp>

// The tests run only where ngspice has the digital models: its own, or the digital.cm the Windows build copies
#define SKIP_WITHOUT_DIGITAL_MODELS()                                                                                  \
    do {                                                                                                               \
        if (!Core::AreDigitalModelsAvailable()) {                                                                      \
            SKIP("This ngspice has no digital models (digital.cm)");                                                   \
        }                                                                                                              \
    } while (false)

#endif // IMCSIM_DIGITAL_MODELS_H

/**
 * @file    digital_models.h
 * @brief   Skips the tests that need the digital code models of ngspice when the ngspice in use has none.
 */

#ifndef IMCSIM_DIGITAL_MODELS_H
#define IMCSIM_DIGITAL_MODELS_H

#include "backends/ngspice/ngspice_library.h"
#include <catch2/catch_test_macros.hpp>

/**
 * @def     SKIP_WITHOUT_DIGITAL_MODELS
 * @brief   Skips the current test when ngspice has no digital code models (`digital.cm`).
 */
#define SKIP_WITHOUT_DIGITAL_MODELS()                                                                                  \
    do {                                                                                                               \
        if (!Core::AreDigitalModelsAvailable()) {                                                                      \
            SKIP("This ngspice has no digital models (digital.cm)");                                                   \
        }                                                                                                              \
    } while (false)

#endif // IMCSIM_DIGITAL_MODELS_H

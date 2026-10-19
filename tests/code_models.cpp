/**
 * @file    code_models.cpp
 * @brief   Points the simulator at the digital code models the build copies next to the tests, before any test runs.
 */

#include "simulator.h"
#include <SDL3/SDL.h>
#include <catch2/catch_test_run_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

// Only the Windows build copies digital.cm beside the tests; elsewhere ngspice loads its own through its spinit file
class CodeModelListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo & /*test_run_info*/) override {
        if (const char *base_path = SDL_GetBasePath()) {
            Core::SetCodeModelDirectory(base_path);
        }
    }
};

} // namespace

CATCH_REGISTER_LISTENER(CodeModelListener)

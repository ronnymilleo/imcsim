/**
 * @file    main.cpp
 * @brief   Program entry point: initializes, runs and shuts down the application.
 */

#include "application.h"

#include "backends/ngspice/ngspice_backend.h"
#include <cstdlib>

/**
 * @brief   Entry point of the desktop application.
 * @param[in] argc  Command-line argument count (unused).
 * @param[in] argv  Command-line argument vector (unused).
 * @return  Process exit status (`EXIT_SUCCESS` on clean shutdown).
 */
int main([[maybe_unused]] const int argc, [[maybe_unused]] char **argv) {
    Core::UseNgspiceByDefault();
    GUI::Application app;
    int result = app.Init();
    if (result == EXIT_SUCCESS) {
        result = app.Run();
    }
    app.Shutdown();
    return result;
}

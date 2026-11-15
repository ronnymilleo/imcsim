/**
 * @file    main.cpp
 * @brief   Program entry point: initializes, runs and shuts down the application.
 */

#include "application.h"

#include "backends/ngspice/ngspice_backend.h"
#include <cstdlib>

int main(int, char **) {
    Core::UseNgspiceByDefault();
    GUI::Application app;
    int result = app.Init();
    if (result == EXIT_SUCCESS) {
        result = app.Run();
    }
    app.Shutdown();
    return result;
}

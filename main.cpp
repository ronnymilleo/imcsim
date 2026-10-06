/**
 * @file    main.cpp
 * @brief   Program entry point: initializes, runs and shuts down the application.
 */

#include "application.h"
#include "error_codes.h"

int main(int, char **) {
    GUI::Application app;
    int result = app.Init();
    if (result == Core::ExitSuccess) {
        result = app.Run();
    }
    app.Shutdown();
    return result;
}

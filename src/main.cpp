#include "core/App.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR commandLine,
                      int showCommand) {
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    vb::core::App app;
    return app.Run(instance);
}

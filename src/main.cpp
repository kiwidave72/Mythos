#include "App.h"
#include <iostream>

int main()
{
    App app;

    if (!app.init(1280, 720, "Mythos"))
    {
        std::cerr << "[Main] App init failed\n";
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}

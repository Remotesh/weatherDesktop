#include <curl/curl.h>
#include "weatherdesktop/App.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    wd::App app;
    if (!app.initialize()) {
        curl_global_cleanup();
        CoUninitialize();
        return 1;
    }

    app.run();
    app.shutdown();

    curl_global_cleanup();
    CoUninitialize();
    return 0;
}

#else // Linux / other POSIX

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    wd::App app;
    if (!app.initialize()) {
        curl_global_cleanup();
        return 1;
    }

    app.run();
    app.shutdown();

    curl_global_cleanup();
    return 0;
}

#endif

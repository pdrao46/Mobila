#include "ui/App.h"
#include "core/Log.h"
#include <exception>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char** argv) {
    (void)argc; (void)argv;
#ifdef _WIN32
    // instância única: dois processos disputariam o mesmo dispositivo/túnel
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MobiladorSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"O Mobilador já está em execução.", L"MOBILADOR", MB_ICONINFORMATION);
        return 0;
    }
#endif
    int rc = 1;
    try {
        mob::App app;
        rc = app.run();
    } catch (const std::exception& e) {
        LOGE("exceção fatal: %s", e.what());
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "MOBILADOR — erro fatal", MB_ICONERROR);
#endif
    }
#ifdef _WIN32
    if (mtx) CloseHandle(mtx);
#endif
    return rc;
}

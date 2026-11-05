// Reach8 - pantalla dividida para Halo: Reach en Halo: The Master Chief Collection (PC).
//
// Se carga como WTSAPI32.dll desde mcc\binaries\win64 y reenvía las funciones
// reales al DLL de Windows. No modifica archivos del juego ni Easy Anti-Cheat:
// si detecta EAC en el proceso, no hace nada.

#include <windows.h>

#include <thread>

#include "core/config.h"
#include "core/crash.h"
#include "core/hooks.h"
#include "core/log.h"
#include "core/paths.h"
#include "input/input.h"
#include "instance/instance.h"
#include "mcc/mcc.h"
#include "players/players.h"
#include "reach/reach_patches.h"
#include "ui/overlay.h"

namespace {

HMODULE g_self = nullptr;
bool g_started = false;

bool is_mcc_process() {
    std::wstring exe = r8::paths::exe_name_lower();
    return exe == L"mcc-win64-shipping.exe" || exe == L"mccwinstore-win64-shipping.exe";
}

bool anti_cheat_present() {
    const wchar_t* modules[] = {L"EasyAntiCheat_x64.dll", L"EasyAntiCheat_EOS.dll",
                                L"EasyAntiCheat.dll"};
    for (auto name : modules) {
        if (GetModuleHandleW(name) != nullptr) return true;
    }
    return false;
}

DWORD WINAPI startup(LPVOID) {
    r8::instance::initialize();
    r8::log::initialize(r8::paths::log_file(r8::instance::index()));
    R8_INFO("Reach8 %s iniciando (proceso %lu)", R8_VERSION, GetCurrentProcessId());

    if (anti_cheat_present()) {
        R8_WARN("Easy Anti-Cheat está activo en este proceso: Reach8 NO se activa. "
                "Inicia MCC con la opción 'Anti-Cheat Disabled' para usar el mod.");
        return 0;
    }

    r8::crash::install(g_self);
    r8::config::load();
    r8::players::initialize(r8::instance::index());

    if (!r8::config::get().enabled) {
        R8_INFO("Reach8 desactivado en reach8.ini ([general] activado = 0)");
        return 0;
    }
    if (!r8::hooks::initialize()) return 0;

    r8::input::initialize();

    if (!r8::mcc::initialize()) {
        R8_ERROR("Reach8 no pudo engancharse a MCC. El juego funciona sin el mod.");
        r8::overlay::write_report();
        return 0;
    }
    r8::reach::initialize(r8::mcc::get_status().profile);
    g_started = true;

    R8_INFO("Reach8 listo. Pulsa F8 (o Back+Start en el mando del jugador 1) en el juego.");
    r8::overlay::write_report();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_self = module;
            DisableThreadLibraryCalls(module);
            if (is_mcc_process()) {
                HANDLE thread = CreateThread(nullptr, 0, startup, nullptr, 0, nullptr);
                if (thread) CloseHandle(thread);
            }
            break;
        case DLL_PROCESS_DETACH:
            // Si el proceso termina (reserved != nullptr) no se toca nada: Windows libera todo.
            if (reserved == nullptr && g_started) {
                r8::overlay::shutdown();
                r8::reach::shutdown();
                r8::input::shutdown();
                r8::hooks::shutdown();
                r8::instance::shutdown();
                r8::crash::uninstall();
                r8::log::shutdown();
            }
            break;
        default:
            break;
    }
    return TRUE;
}

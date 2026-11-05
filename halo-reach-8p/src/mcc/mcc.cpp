#include "mcc/mcc.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "core/config.h"
#include "core/crash.h"
#include "core/hooks.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/paths.h"
#include "input/input.h"
#include "netprobe/netprobe.h"
#include "players/players.h"
#include "ui/overlay.h"

namespace r8::mcc {
namespace {

// Firma publicada en libmcc para el objeto global i_game_manager:
//   call ...; lea rax,[vtable]; mov [game_manager], rax; xor eax,eax
constexpr const char* k_game_manager_aob = "E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? 33 C0";

// RVA conocidos del objeto global (build de marzo de 2025, libmcc).
constexpr uintptr_t k_known_game_manager_steam = 0x3F7B190;
constexpr uintptr_t k_known_game_manager_winstore = 0x3DC9800;

status g_status;
void** g_vtable = nullptr;
std::atomic<XUID> g_primary_xuid{0};

fn_end_frame o_end_frame = nullptr;
fn_get_profile o_get_profile = nullptr;
fn_input_update o_input_update = nullptr;
fn_set_rumble o_set_rumble = nullptr;
fn_get_player o_get_player = nullptr;
fn_get_mapping o_get_mapping = nullptr;

// --- Ganchos -----------------------------------------------------------------

void hk_end_frame(void* self, IDXGISwapChain* swap_chain, unsigned* flags) {
    o_end_frame(self, swap_chain, flags);
    crash::breadcrumb("overlay");
    overlay::on_present(swap_chain);
}

bool hk_get_player(void* self, XUID* xuid, wchar_t* name, uint32_t size, int player) {
    if (player == 0 || !config::enabled()) {
        bool ok = o_get_player(self, xuid, name, size, player);
        if (player == 0 && ok && xuid != nullptr && *xuid != 0) g_primary_xuid.store(*xuid);
        return ok;
    }
    crash::breadcrumb("usuario local extra");
    return players::fill_user(player, xuid, name, size);
}

XUID ensure_primary(void* self) {
    XUID primary = g_primary_xuid.load();
    if (primary == 0 && o_get_player != nullptr) {
        wchar_t name[64] = {};
        XUID x = 0;
        if (o_get_player(self, &x, name, sizeof(name), 0) && x != 0) {
            g_primary_xuid.store(x);
            primary = x;
        }
    }
    return primary;
}

// Los jugadores extra comparten el perfil (sensibilidad, controles, armadura)
// del jugador 1. Se devuelve el puntero original de MCC: no se copia ninguna
// estructura cuyo tamaño pueda cambiar entre versiones.
void* hk_get_profile(void* self, XUID xuid) {
    if (players::is_fake(xuid)) {
        XUID primary = ensure_primary(self);
        if (primary != 0) return o_get_profile(self, primary);
    }
    return o_get_profile(self, xuid);
}

void* hk_get_mapping(void* self, XUID xuid) {
    if (players::is_fake(xuid)) {
        XUID primary = ensure_primary(self);
        if (primary != 0) return o_get_mapping(self, primary);
    }
    return o_get_mapping(self, xuid);
}

bool hk_input_update(void* self, int player, input_state* state) {
    if (overlay::menu_open() && config::block_input_in_menu()) {
        memset(state, 0, sizeof(*state));
        return true;
    }
    int count = players::local_count();
    if (!config::enabled() || count <= 1) return o_input_update(self, player, state);
    if (player < 0 || player >= count) return false;

    if (player == 0 && input::player_uses_km(0)) {
        // Igual que AlphaRing: forzar teclado/ratón para que MCC no mezcle
        // los mandos de los demás jugadores con el jugador 1.
        state->is_km = 1;
        return o_input_update(self, player, state);
    }

    crash::breadcrumb("entrada de mando");
    memset(state, 0, sizeof(*state));
    input::read(player, &state->gamepad);
    return true;
}

void hk_set_rumble(void* self, int player, rumble_state* state) {
    int count = players::local_count();
    if (!config::enabled() || count <= 1 || (player == 0 && input::player_uses_km(0))) {
        o_set_rumble(self, player, state);
        return;
    }
    if (state != nullptr && player >= 0 && player < count) {
        input::rumble(player, state->left_motor_speed, state->right_motor_speed);
    }
}

// --- Localización ------------------------------------------------------------

bool validate_vtable(const memory::module_info& m, void** vtable) {
    auto address = reinterpret_cast<uintptr_t>(vtable);
    if (!memory::in_module(m, address)) return false;
    if (!memory::is_readable(address, sizeof(void*) * slot::count)) return false;
    for (int i = 0; i < slot::count; ++i) {
        auto entry = reinterpret_cast<uintptr_t>(vtable[i]);
        if (!memory::in_module(m, entry) || !memory::is_executable(entry)) return false;
    }
    return true;
}

bool resolve(const memory::module_info& m) {
    // 1) Firma: válida aunque MCC se actualice, mientras el código no cambie.
    for (uintptr_t hit : memory::scan(m, k_game_manager_aob, 8)) {
        auto vtable = reinterpret_cast<void**>(memory::resolve_rip(hit + 5, 3, 7));
        uintptr_t object = memory::resolve_rip(hit + 12, 3, 7);
        if (!memory::in_module(m, object) || !validate_vtable(m, vtable)) continue;
        g_vtable = vtable;
        g_status.vtable_rva = reinterpret_cast<uintptr_t>(vtable) - m.base;
        g_status.game_manager_rva = object - m.base;
        g_status.method = "firma";
        return true;
    }

    // 2) Offset conocido: solo si el objeto ya existe y su tabla es válida.
    uintptr_t known = g_status.which == store::winstore ? k_known_game_manager_winstore
                                                        : k_known_game_manager_steam;
    uintptr_t object = m.base + known;
    for (int attempt = 0; attempt < 300; ++attempt) {  // hasta ~60 s
        if (memory::is_readable(object, sizeof(void*))) {
            auto vtable = *reinterpret_cast<void***>(object);
            if (vtable != nullptr && validate_vtable(m, vtable)) {
                g_vtable = vtable;
                g_status.vtable_rva = reinterpret_cast<uintptr_t>(vtable) - m.base;
                g_status.game_manager_rva = known;
                g_status.method = "offset conocido";
                return true;
            }
        }
        Sleep(200);
    }
    return false;
}

build_profile detect_profile() {
    if (g_status.exe_version == "1.3385.0.0") return build_profile::v1_3385;
    uintptr_t known = g_status.which == store::winstore ? k_known_game_manager_winstore
                                                        : k_known_game_manager_steam;
    if (g_status.game_manager_rva == known) return build_profile::march_2025;
    return build_profile::unknown;
}

template <typename T>
bool hook_slot(int index, T detour, T* original, const char* name) {
    return hooks::vtable_slot(g_vtable, index, reinterpret_cast<void*>(detour),
                              reinterpret_cast<void**>(original), name);
}

}  // namespace

const char* profile_name(build_profile p) {
    switch (p) {
        case build_profile::v1_3385: return "MCC 1.3385.0.0";
        case build_profile::march_2025: return "MCC marzo 2025";
        default: return "desconocida";
    }
}

bool initialize() {
    std::wstring exe = paths::exe_name_lower();
    g_status.which = exe == L"mcc-win64-shipping.exe"         ? store::steam
                     : exe == L"mccwinstore-win64-shipping.exe" ? store::winstore
                                                                 : store::unknown;
    auto module = memory::find_module(nullptr);
    if (!module) return false;
    g_status.exe_version = memory::file_version(module->path);
    R8_INFO("Ejecutable: %s, versión %s", paths::narrow(exe).c_str(), g_status.exe_version.c_str());

    if (!resolve(*module)) {
        R8_ERROR("No se encontró la interfaz i_game_manager. Reach8 queda desactivado "
                 "(el juego sigue funcionando sin cambios). Ejecuta tools/r8scan.py.");
        return false;
    }
    g_status.resolved = true;
    g_status.profile = detect_profile();
    R8_INFO("i_game_manager: tabla en +0x%llX, objeto en +0x%llX (%s). Perfil: %s",
            static_cast<unsigned long long>(g_status.vtable_rva),
            static_cast<unsigned long long>(g_status.game_manager_rva), g_status.method.c_str(),
            profile_name(g_status.profile));

    bool ok = true;
    ok &= hook_slot(slot::signal_end_frame, &hk_end_frame, &o_end_frame, "signal_end_frame");
    ok &= hook_slot(slot::local_user_get_player, &hk_get_player, &o_get_player,
                    "local_user_get_player");
    ok &= hook_slot(slot::local_user_get_profile, &hk_get_profile, &o_get_profile,
                    "local_user_get_profile");
    ok &= hook_slot(slot::local_user_get_gamepad_mapping, &hk_get_mapping, &o_get_mapping,
                    "local_user_get_gamepad_mapping");
    ok &= hook_slot(slot::input_update, &hk_input_update, &o_input_update, "input_update");
    ok &= hook_slot(slot::input_set_rumble, &hk_set_rumble, &o_set_rumble, "input_set_rumble");

    if (!ok) {
        R8_ERROR("Algún gancho de MCC falló: se deshacen todos por seguridad.");
        hooks::remove_all();
        return false;
    }
    g_status.hooks_ok = true;

    if (config::get().netprobe) netprobe::install(g_vtable);
    return true;
}

void shutdown() {
    // Los ganchos de tabla se restauran en hooks::shutdown().
    g_status.hooks_ok = false;
}

const status& get_status() { return g_status; }

XUID primary_xuid() { return g_primary_xuid.load(); }

}  // namespace r8::mcc

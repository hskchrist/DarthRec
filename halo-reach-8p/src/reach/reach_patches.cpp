#include "reach/reach_patches.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

#include "core/config.h"
#include "core/crash.h"
#include "core/hooks.h"
#include "core/log.h"
#include "core/memory.h"

namespace r8::reach {
namespace {

enum class check { function_start, short_jump, none };

struct patch_def {
    const char* name;
    uintptr_t rva;
    const char* bytes;
    check verify;
};

// Parches de AlphaRing (WinterSquire, licencia MIT) para Halo Reach.
// 1) c_network_session::can_accept_any_join_request -> devuelve 0 (sin motivo de rechazo):
//    el motor acepta a los jugadores locales extra en la sesión cooperativa.
// 2) Salto condicional en la lógica de reincorporación cooperativa -> salto incondicional.
const patch_def k_profile_1_3385[] = {
    {"aceptar uniones (coop)", 0x3971C4, "31 C0 C3 90", check::function_start},
    {"reincorporación coop", 0x39489C, "EB", check::short_jump},
};
const patch_def k_profile_march_2025[] = {
    {"aceptar uniones (coop)", 0x397354, "31 C0 C3 90", check::function_start},
    {"reincorporación coop", 0x394A2C, "EB", check::short_jump},
};

struct active_patch {
    patch_def def;
    memory::patch patch;
    std::string state = "pendiente";
};

std::mutex g_mutex;
std::vector<patch_def> g_defs;
std::vector<active_patch> g_active;
uintptr_t g_loaded_base = 0;
std::string g_dll_version = "-";
std::atomic<bool> g_running{false};
std::thread g_watch;

using fn_load_library_ex = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
fn_load_library_ex o_load_library_ex = nullptr;

bool passes(const memory::module_info& m, const patch_def& def, std::string* why) {
    uintptr_t at = m.base + def.rva;
    size_t size = memory::parse_hex(def.bytes).size();
    if (def.rva == 0 || def.rva + size >= m.size) {
        *why = "fuera del DLL";
        return false;
    }
    if (!memory::is_executable(at)) {
        *why = "no es código";
        return false;
    }
    auto p = reinterpret_cast<const uint8_t*>(at);
    switch (def.verify) {
        case check::function_start:
            // MSVC rellena entre funciones con INT3 (CC) o termina la anterior con RET (C3).
            if (p[-1] != 0xCC && p[-1] != 0xC3 && p[-1] != 0x90) {
                *why = "no parece el inicio de una función";
                return false;
            }
            break;
        case check::short_jump:
            if (p[0] < 0x70 || p[0] > 0x7F) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "se esperaba un salto corto, hay %02X", p[0]);
                *why = buffer;
                return false;
            }
            break;
        case check::none:
            break;
    }
    return true;
}

// Requiere g_mutex tomado (comprobar y aplicar es una sola operación atómica,
// así el gancho de carga y la vigilancia periódica no pueden aplicar dos veces).
void apply_all_locked(const memory::module_info& m) {
    crash::breadcrumb("parches haloreach.dll");
    g_dll_version = memory::file_version(m.path);
    g_loaded_base = m.base;
    g_active.clear();

    if (!config::get().reach_patches) {
        R8_INFO("haloreach.dll %s cargado; parches desactivados en la configuración",
                g_dll_version.c_str());
        return;
    }
    if (g_defs.empty()) {
        R8_WARN("haloreach.dll %s: versión de MCC sin parches conocidos. La pantalla dividida "
                "puede no admitir jugadores extra. Ejecuta tools/r8scan.py y añade "
                "'parches_propios' en reach8.ini.",
                g_dll_version.c_str());
        return;
    }

    // Todo o nada: si un parche no pasa las comprobaciones, no se aplica ninguno.
    bool all_ok = true;
    for (const auto& def : g_defs) {
        active_patch a{def, memory::patch(m.base + def.rva, memory::parse_hex(def.bytes), def.name)};
        std::string why;
        if (!passes(m, def, &why)) {
            a.state = "omitido: " + why;
            all_ok = false;
        }
        g_active.push_back(std::move(a));
    }
    if (!all_ok) {
        for (auto& a : g_active) {
            if (a.state == "pendiente") a.state = "omitido: otro parche falló";
        }
        R8_ERROR("haloreach.dll %s: las comprobaciones fallaron; no se aplica ningún parche",
                 g_dll_version.c_str());
        for (auto& a : g_active) R8_ERROR("  %s @+0x%llX: %s", a.def.name,
                                          static_cast<unsigned long long>(a.def.rva), a.state.c_str());
        return;
    }
    for (auto& a : g_active) {
        if (a.patch.apply()) {
            a.state = "aplicado";
            R8_INFO("Parche '%s' aplicado en haloreach.dll+0x%llX (original: %s)", a.def.name,
                    static_cast<unsigned long long>(a.def.rva),
                    memory::to_hex(a.patch.original().data(), a.patch.original().size()).c_str());
        } else {
            a.state = "error al escribir";
            R8_ERROR("Parche '%s' no se pudo escribir", a.def.name);
        }
    }
}

void check_module() {
    std::lock_guard lock(g_mutex);
    auto m = memory::find_module(L"haloreach.dll");
    if (!m) {
        if (g_loaded_base != 0) {
            R8_INFO("haloreach.dll descargado");
            g_loaded_base = 0;
            g_active.clear();  // el código ya no existe: nada que restaurar
        }
        return;
    }
    if (g_loaded_base == m->base) return;
    apply_all_locked(*m);
}

HMODULE WINAPI hk_load_library_ex(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE result = o_load_library_ex(name, file, flags);
    if (result != nullptr && name != nullptr) {
        const wchar_t* tail = wcsrchr(name, L'\\');
        tail = tail ? tail + 1 : name;
        if (_wcsicmp(tail, L"haloreach.dll") == 0) check_module();
    }
    return result;
}

std::vector<patch_def> parse_custom(const std::string& text) {
    std::vector<patch_def> out;
    static std::vector<std::string> storage;  // los punteros de patch_def apuntan aquí
    storage.clear();
    storage.reserve(64);
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';')) {
        size_t a = item.find(':');
        size_t b = a == std::string::npos ? a : item.find(':', a + 1);
        if (a == std::string::npos) continue;
        uintptr_t rva = 0;
        try {
            rva = static_cast<uintptr_t>(std::stoull(item.substr(0, a), nullptr, 16));
        } catch (...) {
            continue;
        }
        std::string bytes = item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        std::string verify = b == std::string::npos ? "" : item.substr(b + 1);
        if (memory::parse_hex(bytes).empty() || storage.size() >= 64) continue;
        storage.push_back(bytes);
        check c = verify.find("salto") != std::string::npos ? check::short_jump
                  : verify.find("ninguna") != std::string::npos ? check::none
                                                                : check::function_start;
        out.push_back({"parche propio", rva, storage.back().c_str(), c});
    }
    return out;
}

}  // namespace

bool initialize(mcc::build_profile profile) {
    auto s = config::get();
    std::vector<patch_def> defs = parse_custom(s.reach_custom_patches);
    if (!defs.empty()) {
        R8_INFO("Usando %zu parche(s) propio(s) de reach8.ini", defs.size());
    } else if (profile == mcc::build_profile::v1_3385) {
        defs.assign(std::begin(k_profile_1_3385), std::end(k_profile_1_3385));
    } else if (profile == mcc::build_profile::march_2025) {
        defs.assign(std::begin(k_profile_march_2025), std::end(k_profile_march_2025));
    }
    {
        std::lock_guard lock(g_mutex);
        g_defs = defs;
    }

    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    void* target = kernelbase ? reinterpret_cast<void*>(GetProcAddress(kernelbase, "LoadLibraryExW"))
                              : nullptr;
    if (!hooks::create(target, reinterpret_cast<void*>(&hk_load_library_ex),
                       reinterpret_cast<void**>(&o_load_library_ex), "LoadLibraryExW")) {
        R8_WARN("Sin gancho de carga de DLL: se vigila haloreach.dll cada medio segundo");
    }

    // Respaldo: comprobación periódica (por si MCC carga el DLL por otra vía).
    g_running.store(true);
    g_watch = std::thread([] {
        while (g_running.load()) {
            check_module();
            Sleep(500);
        }
    });
    return true;
}

void shutdown() {
    if (g_running.exchange(false) && g_watch.joinable()) g_watch.join();
    std::lock_guard lock(g_mutex);
    if (g_loaded_base != 0 && memory::find_module(L"haloreach.dll")) {
        for (auto& a : g_active) a.patch.restore();
    }
    g_active.clear();
}

bool loaded() {
    std::lock_guard lock(g_mutex);
    return g_loaded_base != 0;
}

std::string dll_version() {
    std::lock_guard lock(g_mutex);
    return g_dll_version;
}

std::vector<patch_status> status() {
    std::lock_guard lock(g_mutex);
    std::vector<patch_status> out;
    for (auto& a : g_active) {
        out.push_back({a.def.name, a.def.rva, a.state,
                       memory::to_hex(a.patch.original().data(), a.patch.original().size())});
    }
    if (out.empty()) {
        for (auto& d : g_defs) out.push_back({d.name, d.rva, "esperando haloreach.dll", ""});
    }
    return out;
}

}  // namespace r8::reach

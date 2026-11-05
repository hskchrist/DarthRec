#include "reach/reach_patches.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
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

enum class check { function_start, short_jump, expected_bytes, none };

struct patch_def {
    std::string name;
    uintptr_t rva;
    std::string bytes;
    check verify;
    std::string expected;  // bytes originales esperados (check::expected_bytes)
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
std::vector<active_patch> g_mcc_active;  // parches del ejecutable de MCC (lobby)
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
        *why = "fuera del módulo";
        return false;
    }
    // Con bytes originales comprobados se permite también tocar datos (p. ej. una tabla de
    // límites del lobby); el resto de comprobaciones exigen que sea código.
    if (def.verify != check::expected_bytes && !memory::is_executable(at)) {
        *why = "no es código";
        return false;
    }
    if (!memory::is_readable(at - 1, size + 1)) {
        *why = "memoria no legible";
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
        case check::expected_bytes: {
            auto want = memory::parse_hex(def.expected);
            if (want.empty() || memcmp(p, want.data(), want.size()) != 0) {
                *why = "los bytes originales no coinciden (hay " +
                       memory::to_hex(p, want.empty() ? size : want.size()) + ")";
                return false;
            }
            break;
        }
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
        for (auto& a : g_active) R8_ERROR("  %s @+0x%llX: %s", a.def.name.c_str(),
                                          static_cast<unsigned long long>(a.def.rva), a.state.c_str());
        return;
    }
    for (auto& a : g_active) {
        if (a.patch.apply()) {
            a.state = "aplicado";
            R8_INFO("Parche '%s' aplicado en haloreach.dll+0x%llX (original: %s)", a.def.name.c_str(),
                    static_cast<unsigned long long>(a.def.rva),
                    memory::to_hex(a.patch.original().data(), a.patch.original().size()).c_str());
        } else {
            a.state = "error al escribir";
            R8_ERROR("Parche '%s' no se pudo escribir", a.def.name.c_str());
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

// Formato: rva_hex:bytes_hex:comprobación ; comprobación = inicio_funcion | salto_corto |
// igual=BYTES_ORIGINALES | ninguna. Varios parches separados por ';'.
std::vector<patch_def> parse_custom(const std::string& text, const char* label) {
    std::vector<patch_def> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';')) {
        size_t a = item.find(':');
        if (a == std::string::npos) continue;
        size_t b = item.find(':', a + 1);
        uintptr_t rva = 0;
        try {
            rva = static_cast<uintptr_t>(std::stoull(item.substr(0, a), nullptr, 16));
        } catch (...) {
            continue;
        }
        std::string bytes = item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        std::string verify = b == std::string::npos ? "" : item.substr(b + 1);
        if (memory::parse_hex(bytes).empty()) continue;
        patch_def def{label, rva, bytes, check::function_start, ""};
        if (verify.rfind("igual=", 0) == 0) {
            def.verify = check::expected_bytes;
            def.expected = verify.substr(6);
        } else if (verify.find("salto") != std::string::npos) {
            def.verify = check::short_jump;
        } else if (verify.find("ninguna") != std::string::npos) {
            def.verify = check::none;
        }
        out.push_back(std::move(def));
    }
    return out;
}

// Parches del ejecutable de MCC (por ejemplo, el máximo de jugadores del lobby de
// campaña). Se aplican una vez al iniciar, con las mismas reglas: todo o nada.
void apply_mcc_patches(const std::vector<patch_def>& defs) {
    auto m = memory::find_module(nullptr);
    if (!m || defs.empty()) return;
    std::lock_guard lock(g_mutex);
    bool all_ok = true;
    for (const auto& def : defs) {
        active_patch a{def, memory::patch(m->base + def.rva, memory::parse_hex(def.bytes), def.name)};
        std::string why;
        if (!passes(*m, def, &why)) {
            a.state = "omitido: " + why;
            all_ok = false;
        }
        g_mcc_active.push_back(std::move(a));
    }
    for (auto& a : g_mcc_active) {
        if (!all_ok) {
            if (a.state == "pendiente") a.state = "omitido: otro parche falló";
            R8_ERROR("MCC +0x%llX: %s", static_cast<unsigned long long>(a.def.rva), a.state.c_str());
            continue;
        }
        a.state = a.patch.apply() ? "aplicado" : "error al escribir";
        R8_INFO("Parche de MCC +0x%llX: %s (original: %s)", static_cast<unsigned long long>(a.def.rva),
                a.state.c_str(),
                memory::to_hex(a.patch.original().data(), a.patch.original().size()).c_str());
    }
}

}  // namespace

bool initialize(mcc::build_profile profile) {
    auto s = config::get();
    apply_mcc_patches(parse_custom(s.mcc_custom_patches, "MCC (lobby)"));

    std::vector<patch_def> defs = parse_custom(s.reach_custom_patches, "parche propio");
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
    for (auto& a : g_mcc_active) a.patch.restore();
    g_mcc_active.clear();
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
    for (auto& a : g_mcc_active) {
        out.push_back({a.def.name, a.def.rva, a.state,
                       memory::to_hex(a.patch.original().data(), a.patch.original().size())});
    }
    return out;
}

}  // namespace r8::reach

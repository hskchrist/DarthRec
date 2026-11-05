#include "core/config.h"

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "core/log.h"
#include "core/paths.h"

namespace r8::config {
namespace {

std::mutex g_mutex;
settings g_settings;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_block_in_menu{true};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool to_bool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "si" || v == "sí" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

int to_int(const std::string& v, int fallback) {
    try {
        return std::stoi(v, nullptr, 0);
    } catch (...) {
        return fallback;
    }
}

uint64_t new_seed() {
    GUID guid{};
    CoCreateGuid(&guid);
    uint64_t parts[2];
    static_assert(sizeof(parts) == sizeof(guid));
    memcpy(parts, &guid, sizeof(parts));
    // Se reservan los 8 bits bajos para instancia (4 bits) y jugador (4 bits).
    return (parts[0] ^ parts[1]) & ~0xFFull;
}

std::map<std::string, std::string> parse(const std::wstring& path) {
    std::map<std::string, std::string> values;
    std::ifstream file(path);
    std::string line, section;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        values[section.empty() ? key : section + "." + key] = value;
    }
    return values;
}

}  // namespace

void load() {
    auto v = parse(paths::config_file());
    settings s;

    auto str = [&](const char* key, const std::string& fallback) {
        auto it = v.find(key);
        return it == v.end() ? fallback : it->second;
    };

    s.enabled = to_bool(str("general.activado", "1"), true);
    s.local_players = to_int(str("general.jugadores_locales", "1"), 1);
    if (s.local_players < 1) s.local_players = 1;
    if (s.local_players > k_max_local_players) s.local_players = k_max_local_players;
    s.menu_key = to_int(str("general.tecla_menu", "0x77"), 0x77);
    s.block_game_input_in_menu = to_bool(str("general.bloquear_juego_con_menu", "1"), true);

    for (int i = 0; i < k_max_local_players; ++i) {
        std::string prefix = "jugador" + std::to_string(i + 1) + ".";
        s.players[i].name = str((prefix + "nombre").c_str(), "Jugador " + std::to_string(i + 1));
        s.players[i].device = str((prefix + "dispositivo").c_str(), i == 0 ? "km" : "auto");
    }

    s.reach_patches = to_bool(str("reach.parches", "1"), true);
    s.reach_custom_patches = str("reach.parches_propios", "");

    std::string seed = str("identidad.semilla_xuid", "");
    try {
        s.xuid_seed = seed.empty() ? 0 : std::stoull(seed, nullptr, 16);
    } catch (...) {
        s.xuid_seed = 0;
    }
    bool generated = false;
    if (s.xuid_seed == 0) {
        s.xuid_seed = new_seed();
        generated = true;
    }

    s.window_layout = str("ventana.disposicion", "none");
    s.window_monitor = to_int(str("ventana.monitor", "0"), 0);

    s.netprobe = to_bool(str("investigacion.netprobe", "0"), false);

    s.sdl_wgi = to_bool(str("mandos.windows_gaming_input", "1"), true);
    s.sdl_hidapi = to_bool(str("mandos.hidapi", "1"), true);
    s.sdl_rawinput = to_bool(str("mandos.rawinput", "0"), false);

    set(s);

    R8_INFO("Configuración cargada: %d jugador(es) local(es), parches Reach=%d",
            s.local_players, s.reach_patches ? 1 : 0);

    if (generated || v.empty()) save();
}

void save() {
    settings s = get();
    std::ofstream f(paths::config_file(), std::ios::trunc);
    if (!f) {
        R8_ERROR("No se pudo guardar la configuración");
        return;
    }
    char seed[32];
    snprintf(seed, sizeof(seed), "%016llX", static_cast<unsigned long long>(s.xuid_seed));

    f << "# Reach8 - configuración. Se puede editar con el juego cerrado o desde el menú (F8).\n"
      << "[general]\n"
      << "activado = " << (s.enabled ? 1 : 0) << "\n"
      << "# 1 a 4 jugadores en ESTA ventana del juego. Cambiar antes de iniciar la misión.\n"
      << "jugadores_locales = " << s.local_players << "\n"
      << "# Código de tecla virtual de Windows (0x77 = F8)\n"
      << "tecla_menu = 0x" << std::hex << s.menu_key << std::dec << "\n"
      << "bloquear_juego_con_menu = " << (s.block_game_input_in_menu ? 1 : 0) << "\n\n";

    for (int i = 0; i < k_max_local_players; ++i) {
        f << "[jugador" << (i + 1) << "]\n"
          << "nombre = " << s.players[i].name << "\n"
          << "# km (solo jugador 1), auto, o pad:<clave> (se asigna desde el menú)\n"
          << "dispositivo = " << s.players[i].device << "\n\n";
    }

    f << "[reach]\n"
      << "# Parches conocidos de haloreach.dll (solo se aplican si se reconoce la versión).\n"
      << "parches = " << (s.reach_patches ? 1 : 0) << "\n"
      << "# Parches propios para versiones nuevas de MCC, separados por ';'\n"
      << "# Formato: rva_hex:bytes_hex:comprobación  (comprobación = inicio_funcion | salto_corto | ninguna)\n"
      << "# Ejemplo: 0x397354:31C0C390:inicio_funcion;0x394A2C:EB:salto_corto\n"
      << "parches_propios = " << s.reach_custom_patches << "\n\n"
      << "[identidad]\n"
      << "# Base para los identificadores (XUID) de los jugadores extra. No compartir entre PCs.\n"
      << "semilla_xuid = " << seed << "\n\n"
      << "[ventana]\n"
      << "# none, full, left, right, top, bottom (para 2 instancias: left y right)\n"
      << "disposicion = " << s.window_layout << "\n"
      << "monitor = " << s.window_monitor << "\n\n"
      << "[mandos]\n"
      << "windows_gaming_input = " << (s.sdl_wgi ? 1 : 0) << "\n"
      << "hidapi = " << (s.sdl_hidapi ? 1 : 0) << "\n"
      << "rawinput = " << (s.sdl_rawinput ? 1 : 0) << "\n\n"
      << "[investigacion]\n"
      << "# Cuenta (sin guardar contenido) los paquetes de red que el motor envía por MCC.\n"
      << "netprobe = " << (s.netprobe ? 1 : 0) << "\n";

    R8_INFO("Configuración guardada");
}

settings get() {
    std::lock_guard lock(g_mutex);
    return g_settings;
}

void set(const settings& s) {
    std::lock_guard lock(g_mutex);
    g_settings = s;
    g_enabled.store(s.enabled);
    g_block_in_menu.store(s.block_game_input_in_menu);
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
bool block_input_in_menu() { return g_block_in_menu.load(std::memory_order_relaxed); }

}  // namespace r8::config

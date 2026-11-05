#pragma once

#include <cstdint>
#include <string>

namespace r8::config {

constexpr int k_max_local_players = 4;  // límite del motor por instancia (ver informe §3.2)

struct player_settings {
    std::string name;
    // "km" = teclado y ratón (solo jugador 1), "auto" = primer mando libre,
    // "pad:<clave hex>" = un mando concreto (la clave la genera Reach8).
    std::string device = "auto";
};

struct settings {
    bool enabled = true;
    int local_players = 1;  // 1..4 por instancia
    player_settings players[k_max_local_players];

    int menu_key = 0x77;  // VK_F8
    bool block_game_input_in_menu = true;

    // Parches de haloreach.dll necesarios para que entren jugadores locales extra.
    bool reach_patches = true;
    // Parches propios: "rva:bytes:comprobación;..." (ver config/reach8.ini).
    std::string reach_custom_patches;
    // Parches del ejecutable de MCC (lobby). Mismo formato; usar comprobación igual=.
    std::string mcc_custom_patches;

    uint64_t xuid_seed = 0;  // se genera la primera vez

    // Disposición de ventana: none, full, left, right, top, bottom
    std::string window_layout = "none";
    int window_monitor = 0;

    bool netprobe = false;  // solo investigación: cuenta paquetes del motor

    bool sdl_wgi = true;
    bool sdl_hidapi = true;
    bool sdl_rawinput = false;
};

void load();
void save();

settings get();
void set(const settings& s);

// Lecturas rápidas (sin bloqueo) para los ganchos que se llaman cada fotograma.
bool enabled();
bool block_input_in_menu();

}  // namespace r8::config

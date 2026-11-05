#pragma once

// Estructuras mínimas de la interfaz entre MCC (shell) y los motores de juego.
// Disposición documentada por la comunidad en libmcc (SpringContingency) y usada
// por AlphaRing (WinterSquire, MIT). Solo se declara lo que Reach8 necesita.

#include <cstdint>

struct IDXGISwapChain;

namespace r8::mcc {

using XUID = uint64_t;

constexpr int k_local_player_count = 4;

// Botones con la misma disposición de bits que XINPUT_GAMEPAD.wButtons
// (AlphaRing copia wButtons tal cual y funciona en el juego).
struct gamepad_state {
    uint32_t buttons;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t thumb_lx;
    int16_t thumb_ly;
    int16_t thumb_rx;
    int16_t thumb_ry;
};
static_assert(sizeof(gamepad_state) == 0x10);

struct mouse_state {
    float lx, ly, px, py, lz, lz_scale;
    int32_t buttons;
};
static_assert(sizeof(mouse_state) == 0x1C);

struct input_state {
    int32_t is_km;
    uint8_t keyboard[256];
    mouse_state mouse;
    gamepad_state gamepad;
};
static_assert(sizeof(input_state) == 0x130);

struct rumble_state {
    uint16_t left_motor_speed;
    uint16_t right_motor_speed;
};

// Índices en la tabla virtual i_game_manager (121 entradas en libmcc).
namespace slot {
constexpr int signal_end_frame = 1;
constexpr int local_user_get_profile = 34;
constexpr int input_update = 36;
constexpr int input_update_gamepad = 37;
constexpr int input_set_rumble = 39;
// Transporte de red del motor (nombres según la declaración antigua de libmcc;
// sin confirmar en versiones nuevas: solo se usan para contar paquetes).
constexpr int network_sendto_unreliable = 41;
constexpr int network_sendto_reliable = 42;
constexpr int network_recvfrom = 43;
constexpr int network_send = 44;
constexpr int local_user_get_player = 88;
constexpr int local_user_get_gamepad_mapping = 116;
constexpr int count = 121;
}  // namespace slot

using fn_end_frame = void (*)(void* self, IDXGISwapChain* swap_chain, unsigned* flags);
using fn_get_profile = void* (*)(void* self, XUID xuid);
using fn_input_update = bool (*)(void* self, int player, input_state* state);
using fn_set_rumble = void (*)(void* self, int player, rumble_state* state);
using fn_get_player = bool (*)(void* self, XUID* xuid, wchar_t* name, uint32_t size, int player);
using fn_get_mapping = void* (*)(void* self, XUID xuid);

}  // namespace r8::mcc

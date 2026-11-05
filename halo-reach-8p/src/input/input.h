#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mcc/mcc_types.h"

namespace r8::input {

struct device_info {
    uint64_t key = 0;
    std::string name;
    std::string kind;
    int owner_instance = -1;  // -1 = libre
    int owner_player = -1;
};

bool initialize();
void shutdown();

std::string backend_name();
std::vector<device_info> devices();

// Jugador 1 con teclado y ratón (sin mando propio).
bool player_uses_km(int player);

// Estado del mando asignado al jugador (formato XInput). false = sin mando.
bool read(int player, mcc::gamepad_state* out);
void rumble(int player, uint16_t low, uint16_t high);

// Asignación: el siguiente botón A pulsado en un mando libre se asigna al jugador.
void begin_assign(int player);
void cancel_assign();
int assigning_player();
void unassign(int player);
// Recalcula asignaciones desde la configuración (tras cambiarla en el menú).
void apply_config();
// Clave del mando asignado (0 = ninguno), para guardarla en la configuración.
uint64_t assigned_key(int player);

// Mando del jugador 1 para navegar el menú; true si Back+Start se acaba de pulsar.
bool menu_pad(mcc::gamepad_state* out);
bool menu_combo_pressed();

}  // namespace r8::input

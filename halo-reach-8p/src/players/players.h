#pragma once

#include <cstdint>
#include <string>

#include "mcc/mcc_types.h"

namespace r8::players {

void initialize(unsigned instance);

// Jugadores locales de ESTA instancia (1..4). El motor los pide al iniciar la partida.
int local_count();
void set_local_count(int count);

// Identificadores de los jugadores extra: semilla | instancia << 4 | jugador.
// Únicos entre instancias del mismo PC; la semilla es distinta en cada PC.
bool is_fake(mcc::XUID xuid);
mcc::XUID xuid_for(int player);
std::wstring name_for(int player);

// Responde a MCC::local_user_get_player para los jugadores 2..4.
bool fill_user(int player, mcc::XUID* xuid, wchar_t* name, uint32_t size_bytes);

// Vuelve a leer nombres y número de jugadores de la configuración.
void reload();

// Número global de jugador (1..16) para mostrar: instancia * 4 + jugador + 1.
int global_number(int player);

}  // namespace r8::players

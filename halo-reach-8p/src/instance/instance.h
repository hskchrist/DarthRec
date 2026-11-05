#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace r8::instance {

// Hasta 4 ventanas de MCC en el mismo PC pueden coordinarse (p. ej. 2 × 4 jugadores).
constexpr int k_max_instances = 4;
constexpr int k_max_claims = 32;

// Une esta ventana del juego al bloque compartido "Local\Reach8" y obtiene su número.
bool initialize();
void shutdown();

unsigned index();
int alive_count();

// Reparto de mandos entre instancias: un mando solo puede tenerlo un jugador.
bool claim(uint64_t device_key, int player);
void release(uint64_t device_key);
void release_mine();
// Devuelve true si alguien (de cualquier instancia) tiene el mando.
bool owner(uint64_t device_key, int* instance, int* player);

// Ventana: la disposición se aplica en el hilo de la ventana (mensaje propio).
constexpr UINT k_layout_message = WM_APP + 0x58;
void set_window(HWND hwnd);
HWND window();
void request_layout(const std::string& layout, int monitor);
void apply_layout(HWND hwnd);

}  // namespace r8::instance

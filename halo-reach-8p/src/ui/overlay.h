#pragma once

#include <string>

struct IDXGISwapChain;

namespace r8::overlay {

// Se llama al final de cada fotograma (hilo de render de MCC).
void on_present(IDXGISwapChain* swap_chain);

bool menu_open();
void toast(const std::string& message, float seconds = 6.0f);

// Texto del informe de diagnóstico (también se guarda en Documentos\Reach8).
std::string diagnostics_text();
void write_report();

void shutdown();

}  // namespace r8::overlay

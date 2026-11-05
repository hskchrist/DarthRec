#pragma once

#include <cstdint>

namespace r8::netprobe {

// Herramienta de investigación para la fase de red (5–8 jugadores):
// cuenta llamadas y bytes de las funciones de transporte que el motor usa a
// través de MCC. NO guarda ni modifica el contenido de los paquetes.
struct counters {
    uint64_t calls[4];
    uint64_t bytes[4];
};

void install(void** vtable);
bool installed();
counters snapshot();
const char* slot_name(int i);

}  // namespace r8::netprobe

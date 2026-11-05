#pragma once

namespace r8::crash {

// Registra un manejador que escribe un volcado (minidump) si el fallo ocurre
// dentro del código de Reach8. Los fallos del propio juego los gestiona MCC.
void install(void* own_module);
void uninstall();

// Marca la última operación de Reach8 (aparece en el registro si hay un fallo).
void breadcrumb(const char* what);

}  // namespace r8::crash

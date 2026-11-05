#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace r8::memory {

struct module_info {
    uintptr_t base = 0;
    size_t size = 0;
    std::wstring path;
};

std::optional<module_info> find_module(const wchar_t* name);

// Versión de archivo (p. ej. "1.3385.0.0") del recurso VS_VERSIONINFO.
std::string file_version(const std::wstring& path);

bool in_module(const module_info& m, uintptr_t address);
bool is_executable(uintptr_t address);
bool is_readable(uintptr_t address, size_t size);

// Firma estilo IDA: "48 8D 05 ? ? ? ? 33 C0". Devuelve todas las coincidencias
// en las secciones ejecutables del módulo (máximo max_hits).
std::vector<uintptr_t> scan(const module_info& m, const char* pattern, size_t max_hits = 4);

// Resuelve un desplazamiento relativo a RIP: instr + disp32 en instr+disp_offset.
uintptr_t resolve_rip(uintptr_t instruction, int disp_offset, int instruction_length);

std::vector<uint8_t> parse_hex(const std::string& hex);
std::string to_hex(const uint8_t* data, size_t size);

// Parche reversible: guarda los bytes originales y los restaura al desactivarlo.
class patch {
public:
    patch() = default;
    patch(uintptr_t address, std::vector<uint8_t> bytes, std::string name);

    bool apply();
    void restore();

    bool applied() const { return m_applied; }
    uintptr_t address() const { return m_address; }
    const std::string& name() const { return m_name; }
    const std::vector<uint8_t>& original() const { return m_original; }

private:
    uintptr_t m_address = 0;
    std::vector<uint8_t> m_bytes;
    std::vector<uint8_t> m_original;
    std::string m_name;
    bool m_applied = false;
};

// Escribe un puntero en memoria protegida (entradas de tablas virtuales).
bool write_pointer(void** slot, void* value);

}  // namespace r8::memory

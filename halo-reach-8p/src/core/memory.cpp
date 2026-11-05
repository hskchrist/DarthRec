#include "core/memory.h"

#include <psapi.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace r8::memory {

std::optional<module_info> find_module(const wchar_t* name) {
    HMODULE handle = GetModuleHandleW(name);
    if (handle == nullptr) return std::nullopt;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), handle, &mi, sizeof(mi))) return std::nullopt;
    module_info info;
    info.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    info.size = mi.SizeOfImage;
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(handle, path, static_cast<DWORD>(std::size(path)));
    info.path = path;
    return info;
}

std::string file_version(const std::wstring& path) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return "desconocida";
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return "desconocida";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info)
        return "desconocida";
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
             LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
             LOWORD(info->dwFileVersionLS));
    return buffer;
}

bool in_module(const module_info& m, uintptr_t address) {
    return address >= m.base && address < m.base + m.size;
}

static bool query(uintptr_t address, MEMORY_BASIC_INFORMATION& mbi) {
    return VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == sizeof(mbi) &&
           mbi.State == MEM_COMMIT;
}

bool is_executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!query(address, mbi)) return false;
    constexpr DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & exec) != 0 && (mbi.Protect & PAGE_GUARD) == 0;
}

bool is_readable(uintptr_t address, size_t size) {
    uintptr_t cursor = address;
    uintptr_t end = address + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!query(cursor, mbi)) return false;
        if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return false;
        cursor = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

namespace {

struct pattern_byte {
    uint8_t value;
    bool wildcard;
};

std::vector<pattern_byte> parse_pattern(const char* pattern) {
    std::vector<pattern_byte> out;
    const char* p = pattern;
    while (*p) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (*p == '?') {
            out.push_back({0, true});
            ++p;
            if (*p == '?') ++p;
            continue;
        }
        char hex[3] = {p[0], p[1] ? p[1] : '\0', '\0'};
        out.push_back({static_cast<uint8_t>(strtoul(hex, nullptr, 16)), false});
        p += p[1] ? 2 : 1;
    }
    return out;
}

}  // namespace

std::vector<uintptr_t> scan(const module_info& m, const char* pattern, size_t max_hits) {
    std::vector<uintptr_t> hits;
    auto bytes = parse_pattern(pattern);
    if (bytes.empty()) return hits;

    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m.base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(m.base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        auto start = reinterpret_cast<const uint8_t*>(m.base + section->VirtualAddress);
        size_t size = section->Misc.VirtualSize;
        if (size < bytes.size()) continue;
        if (!is_readable(reinterpret_cast<uintptr_t>(start), size)) continue;

        for (size_t offset = 0; offset + bytes.size() <= size; ++offset) {
            bool match = true;
            for (size_t k = 0; k < bytes.size(); ++k) {
                if (!bytes[k].wildcard && start[offset + k] != bytes[k].value) {
                    match = false;
                    break;
                }
            }
            if (match) {
                hits.push_back(reinterpret_cast<uintptr_t>(start + offset));
                if (hits.size() >= max_hits) return hits;
            }
        }
    }
    return hits;
}

uintptr_t resolve_rip(uintptr_t instruction, int disp_offset, int instruction_length) {
    int32_t disp = 0;
    memcpy(&disp, reinterpret_cast<const void*>(instruction + disp_offset), sizeof(disp));
    return instruction + instruction_length + static_cast<intptr_t>(disp);
}

std::vector<uint8_t> parse_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    std::string digits;
    for (char c : hex) {
        if (std::isxdigit(static_cast<unsigned char>(c))) digits.push_back(c);
    }
    if (digits.size() % 2 != 0) return {};
    for (size_t i = 0; i < digits.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(strtoul(digits.substr(i, 2).c_str(), nullptr, 16)));
    }
    return out;
}

std::string to_hex(const uint8_t* data, size_t size) {
    std::string out;
    char buffer[4];
    for (size_t i = 0; i < size; ++i) {
        snprintf(buffer, sizeof(buffer), i ? " %02X" : "%02X", data[i]);
        out += buffer;
    }
    return out;
}

patch::patch(uintptr_t address, std::vector<uint8_t> bytes, std::string name)
    : m_address(address), m_bytes(std::move(bytes)), m_name(std::move(name)) {}

bool patch::apply() {
    if (m_applied || m_address == 0 || m_bytes.empty()) return m_applied;
    if (!is_readable(m_address, m_bytes.size())) return false;

    m_original.assign(reinterpret_cast<const uint8_t*>(m_address),
                      reinterpret_cast<const uint8_t*>(m_address) + m_bytes.size());
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(m_address), m_bytes.size(),
                        PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(reinterpret_cast<void*>(m_address), m_bytes.data(), m_bytes.size());
    VirtualProtect(reinterpret_cast<void*>(m_address), m_bytes.size(), old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(m_address), m_bytes.size());
    m_applied = true;
    return true;
}

void patch::restore() {
    if (!m_applied) return;
    if (is_readable(m_address, m_original.size())) {
        DWORD old = 0;
        if (VirtualProtect(reinterpret_cast<void*>(m_address), m_original.size(),
                           PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(reinterpret_cast<void*>(m_address), m_original.data(), m_original.size());
            VirtualProtect(reinterpret_cast<void*>(m_address), m_original.size(), old, &old);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(m_address),
                                  m_original.size());
        }
    }
    m_applied = false;
}

bool write_pointer(void** slot, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

}  // namespace r8::memory

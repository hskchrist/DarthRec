#!/usr/bin/env python3
"""r8scan - análisis de solo lectura de MCC y haloreach.dll para Reach8.

Lee (sin modificar) MCC-Win64-Shipping.exe y haloreach.dll, y genera un informe con:
  * versión y fecha de compilación de cada binario,
  * dónde está la interfaz i_game_manager (firma de libmcc),
  * si los parches conocidos de Reach8 encajan con tu haloreach.dll,
  * los bytes alrededor de cada punto, para crear firmas nuevas si MCC se actualizó.

Uso (en Windows, con Python 3.8+):
    python r8scan.py                       (busca MCC en las rutas habituales de Steam)
    python r8scan.py --mcc "D:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection"
    python r8scan.py --self-test           (prueba interna, no necesita el juego)

El informe se guarda en r8scan_informe.txt. No contiene datos personales: solo
versiones, direcciones relativas y bytes de código del juego.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from typing import List, Optional, Tuple

GAME_MANAGER_AOB = "E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? 33 C0"
KNOWN_GAME_MANAGER = {"steam": 0x3F7B190, "winstore": 0x3DC9800}

# Deben coincidir con src/reach/reach_patches.cpp
REACH_PROFILES = {
    "MCC 1.3385.0.0": [
        ("aceptar uniones (coop)", 0x3971C4, "31 C0 C3 90", "inicio_funcion"),
        ("reincorporación coop", 0x39489C, "EB", "salto_corto"),
    ],
    "MCC marzo 2025": [
        ("aceptar uniones (coop)", 0x397354, "31 C0 C3 90", "inicio_funcion"),
        ("reincorporación coop", 0x394A2C, "EB", "salto_corto"),
    ],
}

STEAM_GUESSES = [
    r"C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection",
    r"C:\Program Files\Steam\steamapps\common\Halo The Master Chief Collection",
    r"D:\SteamLibrary\steamapps\common\Halo The Master Chief Collection",
    r"E:\SteamLibrary\steamapps\common\Halo The Master Chief Collection",
    r"D:\Steam\steamapps\common\Halo The Master Chief Collection",
]


class PE:
    """Lector mínimo de PE32+ (x64)."""

    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("no es un ejecutable de Windows (falta MZ)")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("cabecera PE no válida")
        machine, nsections, timestamp = struct.unpack_from("<HHI", data, e_lfanew + 4)
        opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
        self.machine = machine
        self.timestamp = timestamp
        opt = e_lfanew + 24
        self.image_size = struct.unpack_from("<I", data, opt + 56)[0]
        self.sections = []
        sec = opt + opt_size
        for i in range(nsections):
            name = data[sec:sec + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, sec + 8)
            chars = struct.unpack_from("<I", data, sec + 36)[0]
            self.sections.append((name, va, vsize, rawptr, rawsize, chars))
            sec += 40

    def rva_to_offset(self, rva: int) -> Optional[int]:
        for name, va, vsize, rawptr, rawsize, chars in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                off = rva - va
                return rawptr + off if off < rawsize else None
        return None

    def offset_to_rva(self, off: int) -> Optional[int]:
        for name, va, vsize, rawptr, rawsize, chars in self.sections:
            if rawptr <= off < rawptr + rawsize:
                return va + off - rawptr
        return None

    def is_exec_rva(self, rva: int) -> bool:
        for name, va, vsize, rawptr, rawsize, chars in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                return bool(chars & 0x20000000)
        return False

    def exec_sections(self):
        for s in self.sections:
            if s[5] & 0x20000000:
                yield s

    def read_rva(self, rva: int, size: int) -> Optional[bytes]:
        off = self.rva_to_offset(rva)
        if off is None or off + size > len(self.data):
            return None
        return self.data[off:off + size]


def parse_pattern(pattern: str) -> List[Optional[int]]:
    out: List[Optional[int]] = []
    for tok in pattern.split():
        out.append(None if tok.startswith("?") else int(tok, 16))
    return out


def scan(pe: PE, pattern: str, max_hits: int = 8) -> List[int]:
    """Devuelve RVAs de las coincidencias en secciones ejecutables."""
    pat = parse_pattern(pattern)
    # Prefijo fijo más largo para acelerar la búsqueda con bytes.find
    best_start, best_len = 0, 0
    i = 0
    while i < len(pat):
        if pat[i] is None:
            i += 1
            continue
        j = i
        while j < len(pat) and pat[j] is not None:
            j += 1
        if j - i > best_len:
            best_start, best_len = i, j - i
        i = j
    anchor = bytes(pat[best_start:best_start + best_len])  # type: ignore[arg-type]
    hits: List[int] = []
    for name, va, vsize, rawptr, rawsize, chars in pe.exec_sections():
        blob = pe.data[rawptr:rawptr + rawsize]
        pos = blob.find(anchor)
        while pos != -1:
            start = pos - best_start
            if 0 <= start <= len(blob) - len(pat):
                if all(p is None or blob[start + k] == p for k, p in enumerate(pat)):
                    hits.append(va + start)
                    if len(hits) >= max_hits:
                        return hits
            pos = blob.find(anchor, pos + 1)
    return hits


def resolve_rip(pe: PE, rva: int, disp_offset: int, length: int) -> Optional[int]:
    raw = pe.read_rva(rva + disp_offset, 4)
    if raw is None:
        return None
    disp = struct.unpack("<i", raw)[0]
    return rva + length + disp


def file_version(data: bytes) -> str:
    sig = b"\xbd\x04\xef\xfe"
    pos = data.find(sig)
    if pos < 0:
        return "desconocida"
    ms, ls = struct.unpack_from("<II", data, pos + 8)
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"


def hexdump(b: Optional[bytes]) -> str:
    return " ".join(f"{x:02X}" for x in b) if b else "(ilegible)"


def check_patch(pe: PE, rva: int, size: int, verify: str) -> Tuple[bool, str]:
    if not pe.is_exec_rva(rva):
        return False, "no es código"
    cur = pe.read_rva(rva - 1, size + 1)
    if cur is None:
        return False, "fuera del archivo"
    prev, first = cur[0], cur[1]
    if verify == "inicio_funcion" and prev not in (0xCC, 0xC3, 0x90):
        return False, f"no parece inicio de función (byte anterior {prev:02X})"
    if verify == "salto_corto" and not (0x70 <= first <= 0x7F):
        return False, f"se esperaba un salto corto (70-7F), hay {first:02X}"
    return True, "encaja"


def find_mcc(root: Optional[str]) -> Optional[str]:
    candidates = [root] if root else STEAM_GUESSES
    for c in candidates:
        if not c:
            continue
        for sub in ("", "mcc\\binaries\\win64", "mcc/binaries/win64"):
            p = os.path.join(c, sub) if sub else c
            if os.path.isfile(os.path.join(p, "MCC-Win64-Shipping.exe")) or os.path.isfile(
                    os.path.join(p, "MCCWinStore-Win64-Shipping.exe")):
                return p
    return None


def analyse(win64: str, lines: List[str]) -> None:
    def out(s: str = "") -> None:
        print(s)
        lines.append(s)

    exe_name = "MCC-Win64-Shipping.exe"
    store = "steam"
    if not os.path.isfile(os.path.join(win64, exe_name)):
        exe_name, store = "MCCWinStore-Win64-Shipping.exe", "winstore"
    exe_path = os.path.join(win64, exe_name)
    root = os.path.abspath(os.path.join(win64, "..", "..", ".."))
    reach_path = os.path.join(root, "haloreach", "haloreach.dll")

    out("=== Reach8 r8scan ===")
    out(f"Carpeta win64: {win64}")

    with open(exe_path, "rb") as f:
        exe_data = f.read()
    exe = PE(exe_data)
    exe_version = file_version(exe_data)
    out(f"{exe_name}: versión {exe_version}, {len(exe_data)} bytes, "
        f"marca de tiempo 0x{exe.timestamp:08X}")

    hits = scan(exe, GAME_MANAGER_AOB)
    out(f"Firma i_game_manager: {len(hits)} coincidencia(s)")
    gm_rva = None
    for h in hits:
        vt = resolve_rip(exe, h + 5, 3, 7)
        obj = resolve_rip(exe, h + 12, 3, 7)
        out(f"  en +0x{h:X}: tabla +0x{vt:X}, objeto +0x{obj:X}" if vt and obj else f"  en +0x{h:X}")
        if gm_rva is None and obj:
            gm_rva = obj
    profile = None
    if exe_version == "1.3385.0.0":
        profile = "MCC 1.3385.0.0"
    elif gm_rva == KNOWN_GAME_MANAGER[store]:
        profile = "MCC marzo 2025"
    out(f"Perfil de build: {profile or 'DESCONOCIDO (versión nueva de MCC)'}")

    proxy = os.path.join(win64, "WTSAPI32.dll")
    out(f"WTSAPI32.dll en win64: {'sí (Reach8 u otro mod instalado)' if os.path.isfile(proxy) else 'no'}")

    out()
    if not os.path.isfile(reach_path):
        out(f"No se encontró {reach_path}")
        return
    with open(reach_path, "rb") as f:
        reach_data = f.read()
    reach = PE(reach_data)
    out(f"haloreach.dll: versión {file_version(reach_data)}, {len(reach_data)} bytes, "
        f"marca de tiempo 0x{reach.timestamp:08X}")

    for name, defs in REACH_PROFILES.items():
        out(f"\nParches del perfil '{name}':")
        all_ok = True
        for pname, rva, patch, verify in defs:
            size = len(patch.split())
            ok, why = check_patch(reach, rva, size, verify)
            all_ok &= ok
            around = reach.read_rva(rva - 16, 48)
            out(f"  {pname} @+0x{rva:X}: {why}")
            out(f"    bytes [-16..+32]: {hexdump(around)}")
        out(f"  => {'ENCAJA con tu haloreach.dll' if all_ok else 'no encaja'}")

    out("\nEnvía este informe (r8scan_informe.txt) para preparar parches de tu versión.")


def self_test() -> int:
    # Ejecutable sintético: una sección ejecutable con la firma i_game_manager.
    text = bytearray(0x200)
    code = bytes.fromhex("E8 00 00 00 00 48 8D 05 10 00 00 00 48 89 05 20 00 00 00 33 C0")
    text[0x40:0x40 + len(code)] = code
    hdr = bytearray(0x200)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    hdr[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", hdr, 0x84, 0x8664, 1, 0x12345678)
    struct.pack_into("<H", hdr, 0x80 + 20, 0xF0)
    struct.pack_into("<I", hdr, 0x80 + 24 + 56, 0x2000)
    sec = 0x80 + 24 + 0xF0
    hdr[sec:sec + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", hdr, sec + 8, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", hdr, sec + 36, 0x60000020)
    data = bytes(hdr) + bytes(text) + b"\xbd\x04\xef\xfe" + b"\0" * 4 + struct.pack(
        "<II", (1 << 16) | 3528, 0)
    pe = PE(data)
    hits = scan(pe, GAME_MANAGER_AOB)
    assert hits == [0x1040], hits
    assert resolve_rip(pe, 0x1045, 3, 7) == 0x1045 + 7 + 0x10
    assert resolve_rip(pe, 0x104C, 3, 7) == 0x104C + 7 + 0x20
    assert file_version(data) == "1.3528.0.0", file_version(data)
    assert pe.rva_to_offset(0x1040) == 0x240
    assert pe.is_exec_rva(0x1040)
    ok, _ = check_patch(pe, 0x1040, 1, "salto_corto")
    assert not ok
    print("r8scan: prueba interna correcta")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Análisis de solo lectura para Reach8")
    ap.add_argument("--mcc", help="carpeta de Halo The Master Chief Collection")
    ap.add_argument("--out", default="r8scan_informe.txt")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()

    win64 = find_mcc(args.mcc)
    if not win64:
        print("No encuentro MCC. Indica la carpeta: python r8scan.py --mcc \"RUTA\"")
        return 1
    lines: List[str] = []
    try:
        analyse(win64, lines)
    except (OSError, ValueError, struct.error) as e:
        print(f"Error: {e}")
        return 1
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nInforme guardado en {os.path.abspath(args.out)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

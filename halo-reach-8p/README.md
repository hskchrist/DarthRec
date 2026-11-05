# Reach8 — pantalla dividida para Halo: Reach (MCC, PC)

Mod para *Halo: The Master Chief Collection* (Steam / Microsoft Store) que añade
**pantalla dividida local** a la campaña de **Halo: Reach**, con mandos
independientes, nombres propios y un menú en el juego.

> **Estado: versión 0.1 experimental, sin probar todavía en el juego real.**
> El código compila, pero se escribió sin acceso a MCC. Lee
> [Qué funciona y qué no](#qué-funciona-y-qué-no) antes de usarlo.

---

## Qué funciona y qué no

| Función | Estado |
|---|---|
| 1–4 jugadores en una ventana del juego (pantalla dividida del propio motor) | **Implementado.** Pendiente de probar en tu PC |
| Cada jugador con su mando, nombre e identificador único | **Implementado** |
| Hasta 16 mandos (Xbox, PlayStation, Switch, genéricos) vía SDL3 | **Implementado.** Pendiente de probar con >4 mandos Xbox |
| Menú en el juego (F8 o Back+Start del jugador 1) | **Implementado** |
| Diagnóstico, registro de errores, volcados de fallos | **Implementado** |
| 2 ventanas del juego en el mismo PC, colocadas lado a lado, con mandos repartidos | **Implementado** (cada ventana con sus 4 jugadores) |
| **Unir esas 2 ventanas en la MISMA campaña (jugadores 5–8)** | **No implementado.** Ver abajo |
| Menú "traer / ocultar" jugadores junto al jugador 1 | **No implementado** (falta localizar los objetos de Reach en memoria) |
| Versiones de MCC posteriores a marzo de 2025 | La interfaz de MCC se busca por firma (debería seguir funcionando). Los parches de `haloreach.dll` necesitan offsets de tu versión: usa `r8scan.py` |

### Por qué 5–8 jugadores todavía no

El motor de Reach admite **como máximo 4 jugadores locales por proceso**: es una
constante de compilación usada en las cámaras, la entrada, el HUD y la tabla de
pantalla dividida (ver `docs/INFORME_TECNICO_VIABILIDAD.md`, §3.2). Para 8 en un
PC hacen falta **2 ventanas del juego de 4 jugadores unidas por red**.

Con **una sola cuenta de Steam** no se puede usar la red de Steam entre las dos
ventanas (sería la misma cuenta uniéndose a sí misma). Haría falta un "puente"
de red propio entre las dos ventanas. Reach8 0.1 incluye la base para eso:
memoria compartida entre ventanas, reparto de mandos, colocación de ventanas y
una sonda de red (`netprobe`) para investigar el tráfico. **El puente no existe
todavía.** Es la siguiente fase y no hay garantía de que sea posible.

---

## Requisitos

- Windows 10/11 de 64 bits.
- Halo MCC en Steam (también debería funcionar la versión de Microsoft Store).
- Iniciar MCC con **"Anti-Cheat Disabled (Mods and Limited Services)"**.
  Reach8 **no toca Easy Anti-Cheat**: si lo detecta, no se activa.
- Juego en **modo ventana** o **ventana sin bordes** si vas a usar 2 ventanas.

## Instalación

1. Descarga el paquete `Reach8-windows-x64` (en GitHub: pestaña *Actions* →
   última ejecución verde de "Compilar Reach8 (Windows)" → *Artifacts*).
2. Descomprímelo y haz clic derecho en `install.ps1` → **Ejecutar con PowerShell**.
   - Busca MCC solo. Si no lo encuentra:
     `powershell -ExecutionPolicy Bypass -File install.ps1 -MccPath "D:\SteamLibrary\steamapps\common\Halo The Master Chief Collection"`
   - Si ya tenías otro `WTSAPI32.dll` (por ejemplo AlphaRing), se guarda en
     `mcc\binaries\win64\Reach8_backup\`. **Reach8 y AlphaRing no pueden estar
     instalados a la vez** (usan el mismo nombre de DLL).
3. Instalación manual: copia `WTSAPI32.dll` a
   `...\Halo The Master Chief Collection\mcc\binaries\win64\`.

**Archivos originales:** Reach8 no modifica ningún archivo del juego. Los parches
se hacen en memoria al cargar `haloreach.dll` y desaparecen al cerrar el juego.

**Desinstalar:** ejecuta `uninstall.ps1`. Quita Reach8 y restaura la copia de
seguridad si existe. Hazlo **antes de jugar con antitrampas** (matchmaking).

## Uso: 1 a 4 jugadores en un PC

1. Conecta los mandos **antes** de abrir el juego (también se detectan después).
2. Inicia MCC con *Anti-Cheat Disabled*.
3. En el menú principal pulsa **F8** (o **Back + Start** en el mando del jugador 1).
4. Pestaña **Jugadores**:
   - Elige cuántos jugadores (1–4) **antes de iniciar la misión**.
   - Escribe el nombre de cada uno.
   - Pulsa **Asignar mando** y luego **A** en el mando de ese jugador.
     **Auto** coge el primer mando libre. El jugador 1 puede usar **Teclado/ratón**.
   - **Guardar configuración** (se guarda en `Documentos\Reach8\reach8.ini`).
5. Cierra el menú (F8) y empieza la campaña de Reach. El motor creará la pantalla
   dividida para el número de jugadores elegido.

Si ya estás dentro de una misión, cambia el número de jugadores y **reinicia la
misión** desde el menú de pausa.

## Configuración de controles

- Cada jugador usa los **mismos ajustes que el jugador 1** en MCC (sensibilidad,
  botones, inversión, armadura). MCC solo guarda un perfil por cuenta.
- Mandos compatibles: Xbox 360/One/Series, DualShock 4, DualSense, Switch Pro y
  la mayoría de genéricos (SDL3).
- Botones en el menú: stick izquierdo / cruceta para moverse, **A** aceptar,
  **B** atrás, **Back + Start** abrir/cerrar.
- Mientras el menú está abierto, el juego no recibe entrada.
- Opciones de `[mandos]` en `reach8.ini`: `windows_gaming_input`, `hidapi`,
  `rawinput` (déjalas como están salvo que algún mando no aparezca).

## Uso experimental: 2 ventanas en el mismo PC

Sirve para preparar la fase de 8 jugadores. **Hoy cada ventana juega su propia
partida**: los jugadores de una ventana no ven a los de la otra.

1. Abre MCC normal (ventana 1).
2. Abre la segunda ventana ejecutando `MCC-Win64-Shipping.exe` desde
   `mcc\binaries\win64` (si Steam lo permite con tu cuenta; es la prueba que te pedí).
3. En cada ventana: F8 → **Ventana** → *Mitad izquierda* o *Mitad derecha*.
4. En cada ventana asigna sus mandos. Un mando asignado en una ventana aparece
   como ocupado en la otra (pestaña **Mandos**).

## Diagnóstico y errores

- Registro: `Documentos\Reach8\logs\reach8_instancia1.log` (una por ventana).
- Informe: F8 → **Diagnóstico** → *Guardar informe* →
  `Documentos\Reach8\diagnostico.txt`.
- Si Reach8 falla dentro de su propio código, deja un volcado en
  `Documentos\Reach8\crash\` y el motivo en el registro.
- Comprobaciones de seguridad:
  - Si no encuentra la interfaz de MCC, se desactiva solo y el juego sigue normal.
  - Si algún gancho falla, deshace todos.
  - Los parches de `haloreach.dll` se aplican **todos o ninguno**, solo si la
    versión es conocida y los bytes tienen la forma esperada.

### Si MCC se actualizó

1. Ejecuta `python r8scan.py` (solo lee archivos). Genera `r8scan_informe.txt`.
2. Envíame ese informe. Con él se preparan los parches de tu versión, que se
   ponen en `reach8.ini` → `[reach] parches_propios` sin recompilar nada.

## Compilar desde el código

Visual Studio 2022 (C++ de escritorio) y CMake 3.24+:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

El resultado es `build\Release\WTSAPI32.dll`. Las dependencias (MinHook, Dear
ImGui, SDL3) se descargan solas en versiones fijas. También se compila en cada
cambio con GitHub Actions (`.github/workflows/reach8.yml`).

## Estructura

```
halo-reach-8p/
├─ src/
│  ├─ main.cpp              Arranque, comprobación de EAC, orden de inicio
│  ├─ proxy/                Reenvío de WTSAPI32 al DLL de Windows
│  ├─ core/                 Registro, configuración, memoria/firmas, hooks, fallos
│  ├─ mcc/                  Interfaz i_game_manager de MCC (usuarios, perfiles, entrada)
│  ├─ reach/                Parches de haloreach.dll (cooperativo local)
│  ├─ players/              Jugadores 2–4: nombre e identificador único
│  ├─ input/                Mandos con SDL3 (o XInput) y asignación
│  ├─ instance/             Coordinación entre ventanas del juego y colocación
│  ├─ netprobe/             Sonda de red para investigar (apagada por defecto)
│  └─ ui/                   Menú en pantalla (Dear ImGui)
├─ config/reach8.ini        Configuración por defecto comentada
├─ tools/                   install.ps1, uninstall.ps1, r8scan.py
└─ docs/                    Informe técnico, pruebas, limitaciones
```

## Créditos y licencias

- Investigación de la interfaz de MCC y de los parches de Reach:
  [AlphaRing](https://github.com/WinterSquire/AlphaRing) (WinterSquire, MIT) y
  [libmcc](https://github.com/SpringContingency/libmcc) (SpringContingency).
  Reach8 no copia su código: reimplementa las estructuras y parches documentados.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2),
  [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [SDL3](https://github.com/libsdl-org/SDL) (zlib).
- Halo y MCC son marcas de Microsoft. Mod no oficial, sin afiliación. Requiere
  una copia legítima del juego.

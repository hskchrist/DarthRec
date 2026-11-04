# Halo Reach 8 Players — Informe técnico de viabilidad (Fase 0)

> Proyecto: mod para *Halo: The Master Chief Collection* (Steam), título **Halo: Reach**.
> Objetivo: campaña completa con **8 jugadores simultáneos**.
> Estado: **investigación**. No se ha escrito código. Este documento termina con una
> propuesta de arquitectura que **queda pendiente de tu aprobación**.
> Fecha: 4 de noviembre de 2026.

---

## 0. Cómo leer este informe (nivel de certeza)

Cada afirmación lleva una etiqueta según de dónde sale:

| Etiqueta | Significado |
|---|---|
| **[V]** | **Verificado** leyendo código fuente público de mods existentes (AlphaRing, libmcc). Son estructuras y parches que ya funcionan en el juego real, pero con *offsets* atados a versiones concretas de MCC. |
| **[O]** | Fuente **oficial** (Halo Support / Halo Waypoint / Microsoft Learn). |
| **[C]** | Información de la **comunidad** (foros de Steam, Reddit, prensa). Útil, pero sin verificar. |
| **[I]** | **Inferencia técnica** mía a partir del linaje del motor Blam (Halo 3 → ODST → Reach). Razonable, pero **no comprobada** en el `haloreach.dll` actual. |

**Limitación importante del análisis:** este trabajo se hizo en un contenedor Linux en la nube, sin acceso
a tu copia del juego. **No he desensamblado `haloreach.dll` ni `MCC-Win64-Shipping.exe`.** Todo lo marcado
como [I] debe confirmarse en la Fase 1 (análisis estático en tu PC con tus binarios). Además, las pruebas
de los apartados 6 (4→8 jugadores) solo se pueden hacer en Windows con el juego instalado y mandos reales.

### Fuentes de código revisadas

- **AlphaRing** (WinterSquire, licencia MIT) — mod de pantalla dividida para todos los juegos de MCC.
  Ramas `dev` (marzo de 2025) y `master` (diciembre de 2024).
- **libmcc** (SpringContingency, nov. 2025) — biblioteca de solo cabeceras con las estructuras de memoria de MCC
  y de cada juego. Es la dependencia que usa AlphaRing.

---

## 1. Respuestas directas

| Pregunta | Respuesta corta |
|---|---|
| **¿Es técnicamente posible?** | **8 jugadores en una campaña: posiblemente viable.** **8 jugadores en una sola instancia del juego con 8 vistas: no viable** con las herramientas actuales. |
| **¿Qué partes del motor habría que modificar?** | (1) El límite de jugadores de la **sesión cooperativa** (MCC + motor). (2) La **aparición** (*spawn*) y la **reaparición** de los jugadores 5–8. (3) El comportamiento de los **scripts de campaña** con más de 4 jugadores. (4) Para un solo PC: la **asignación de mandos** y la **disposición de ventanas** de 2 instancias. |
| **¿Qué método tiene más posibilidades?** | **Red (Modo B) primero.** Pantalla dividida "8 en 1" solo como **híbrido: 2 instancias × 4 jugadores** cada una, unidas por red. |
| **¿8 jugadores en pantalla dividida?** | **No en una sola instancia.** El motor tiene un máximo de 4 usuarios locales fijado al compilar. **Posiblemente sí** con 2 instancias de 4 vistas cada una. |
| **¿Es más viable por LAN?** | **Sí.** 343 ya subió el Tiroteo (*Firefight*) personalizado de Reach de 4 a 8 jugadores en 2023 [C]. El motor de Reach soporta 16 jugadores en multijugador. El cuello de botella de la red es un límite de sesión, no una matriz de tamaño fijo. Ojo: MCC en PC **no tiene LAN real sin Internet** que se conozca. "LAN" aquí significa partida personalizada online entre PCs (ver §3.9). |
| **¿Qué podría impedir el proyecto?** | (a) Límites de campaña escritos en los scripts y en los datos de cada mapa. (b) Que el motor rechace más de 4 jugadores en el modo campaña de un modo que no se pueda parchear de forma segura. (c) Las actualizaciones de MCC rompen los *offsets*. (d) La carga de rendimiento de 8 vistas. (e) Ejecutar dos instancias de MCC en un PC. |

---

## 2. Arquitectura de Halo Reach dentro de MCC

```
MCC-Win64-Shipping.exe  (shell en Unreal Engine 4)
 ├─ Menús, lobby, perfiles, ajustes, logros, servicios online (Steam / Xbox Live)
 ├─ Lectura de mandos (XInput) y teclado/ratón
 ├─ Transporte de red (envía y recibe los paquetes del motor)
 │
 └─ carga dinámicamente ──►  haloreach\haloreach.dll  (motor Blam de Reach, 64 bits)
                              ├─ simulación, IA, scripts (HaloScript), física
                              ├─ cámaras/observadores, render D3D11, HUD
                              └─ mapas: haloreach\maps\*.map (tags compilados)
```

- **[V]** La interfaz entre el shell y el motor es una tabla virtual (`i_game_manager`). Según libmcc,
  el motor llama a funciones del shell para:
  - **Usuarios locales:** `local_user_get_player(index)` devuelve XUID y nombre del usuario local `index`.
  - **Perfiles:** `get_player_profile(XUID)`.
  - **Entrada:** `get_input_state(e_local_player, …)` y `set_input_state` (vibración).
  - **Red:** `network_sendto_reliable`, `network_sendto_unreliable` y `network_recvfrom`. **El motor no abre
    sus propios sockets. Delega el transporte en MCC.**
- **[V]** Al lanzar una partida, MCC pasa al motor una estructura `s_game_options`. Contiene `game_mode`
  (campaña, Tiroteo, multijugador…), el mapa, la dificultad, las calaveras y `s_player_options`, con
  **16 ranuras de jugador** (`player_options[16]`, XUID + dirección) y `peer_count`.
- **[V]** Hay dos ejecutables con *offsets* distintos: **Steam** (`MCC-Win64-Shipping.exe`) y **Microsoft Store**
  (`MCCWinStore-Win64-Shipping.exe`). AlphaRing mantiene tablas separadas para cada uno.
- **[V]** Los mods existentes se cargan con un **DLL proxy** (`WTSAPI32.dll`) en `mcc\binaries\win64`. Funcionan
  enganchando (*hooking*) la tabla `i_game_manager`, los estados del ciclo de juego y parches de bytes en `haloreach.dll`.
- **[O]** Los mods **solo funcionan con Easy Anti-Cheat desactivado**. Se elige la opción
  *"Anti-Cheat Disabled (Mods and Limited Services)"* en Steam. Así funcionan la campaña y las partidas
  personalizadas online. El matchmaking, los logros y la XP quedan bloqueados. **Este proyecto no tocará EAC:
  usaremos el modo oficial sin antitrampas.**
- **[O]** Existen herramientas oficiales de Reach (*Halo: Reach Mod Tools*, sept. 2022): Foundation, Tool, Sapien
  y MegaloEdit. Permiten **recompilar mapas y scripts de campaña**. Este dato es clave para §3.7.
- **[C]** Cada parche de MCC desplaza los *offsets*. AlphaRing dejó de funcionar tras la actualización de
  marzo de 2025 y tuvo que actualizarse.

**Clasificación:** comprensión de la arquitectura → **Viable** (bien documentada por la comunidad).

---

## 3. Análisis por sistema

### 3.1 Creación y registro de jugadores

Hay **dos niveles** distintos que no conviene confundir:

1. **Nivel sesión (MCC + red):** quién está en la partida.
   - **[V]** `s_player_options` tiene 16 ranuras y `peer_count`. **La estructura no limita a 4.**
   - **[V]** AlphaRing aplica en Reach dos parches llamados `COOP_JOIN` (sustituye una función por
     `xor eax,eax; ret`) y `COOP_REJOIN` (cambia un salto condicional por `jmp`). Hacen que el motor acepte
     jugadores locales adicionales en cooperativo. libmcc también localiza en Reach la función
     `c_network_session::can_accept_any_join_request`, el punto natural donde se decide si entra otro jugador.
   - **[V]** En **Halo 4**, AlphaRing necesita un parche extra, `COOP_PLAYER_LIMIT` (6 NOPs). Esto demuestra que,
     al menos en ese título, **existe una comprobación explícita del límite de jugadores de campaña** dentro del
     DLL. En Reach no se ha publicado el equivalente. **Hay que localizarlo** (ver §3.7).
2. **Nivel motor (simulación):** la tabla de jugadores y sus Spartans.
   - **[I]** En la familia Blam, la tabla `players` es un *data array* de **16 entradas** en todos los modos. Reach
     admite 16 jugadores en multijugador, así que la tabla no debería ser el límite.
   - **[V]** El motor tiene una función nativa para añadir un jugador local (`local_player_add(name, tag)`). Aparece
     comentada en el código de AlphaRing para Reach, H3, ODST y H4. Es útil para diagnóstico.

**Clasificación:**
- Registrar jugadores 5–8 en la sesión → **Posiblemente viable** (hay que encontrar y parchear el límite de campaña).
- Tabla de jugadores del motor con 8 entradas → **Posiblemente viable** ([I], muy probable, por verificar).

### 3.2 Límite de jugadores locales (el problema central de la pantalla dividida)

**Esto no es un número configurable.** El "4" de los jugadores locales es una **constante de compilación**
que dimensiona muchas matrices estáticas:

| Estructura | Tamaño | Fuente |
|---|---|---|
| `e_local_player` (interfaz MCC ↔ motor) | `_local_player_0 … _local_player_3`, `k_local_player_count = 4` | **[V]** libmcc |
| Halo Reach `c_splitscreen_config::m_config_table` | `array<array<s_splitscreen_view_config, 4>, 5>`: 5 configuraciones (0–4 jugadores) × **4 vistas** | **[V]** libmcc (offset de Reach `0xB43C40`) |
| Halo 3 `s_observer_globals.observers` (cámaras) | `[k_local_player_count]` = 4, tamaño total fijo `0xF50` | **[V]** libmcc |
| Halo 3 `player_control` (entrada por usuario) | `input_user_states[4]`, `input_states[4]`, `output_state[4]` | **[V]** libmcc |
| Halo 3 `g_player_profile_globals` | `array<…, k_local_player_count>` | **[V]** libmcc |
| HUD (chud), oyentes de sonido, vistas de render, máscaras de bits de usuario en Reach | 4 | **[I]** mismo linaje que H3 |

Para ampliar a 8 usuarios locales en **una** instancia habría que:
- **reubicar** cada una de esas matrices, que viven dentro del *game state* (bloque contiguo de tamaño fijo que se
  guarda en los puntos de control y en las películas);
- **reescribir** cada bucle `for (i < 4)`, cada máscara de 4 bits y cada índice empaquetado en el código máquina;
- **añadir** disposiciones de 5–8 vistas que el motor no tiene, en la tabla de *split-screen* y en los datos de HUD.

Sin código fuente, son cientos de puntos de parcheo en un binario de 64 bits que cambia con cada actualización.

**Clasificación:**
- Hasta **4 jugadores locales por instancia** → **Viable** ([V] AlphaRing ya lo hace en todos los juegos de MCC).
- **5–8 jugadores locales en una instancia** → **No viable con las herramientas actuales.**

### 3.3 Sistema de identificación de jugadores

- **[V]** Cada jugador se identifica por un **XUID** de 64 bits y un nombre. AlphaRing genera XUID para los locales
  extra (GUID aleatorio + índice) y guarda nombres y *service tags* en un `profile.dat` propio.
- **[V]** Los perfiles por jugador (`s_player_profile`, `0xACC` bytes) incluyen sensibilidad, FOV, inversión,
  colores, armadura y asignación de botones. Se pueden servir por XUID.
- En red, cada PC aporta el XUID de su propia cuenta, que es único por diseño.
- Riesgo: dos instancias en el mismo PC que generen el mismo XUID, o dos PCs con la misma semilla. Se evita con
  XUID derivados de un GUID de instalación + índice + id de instancia, y comprobándolos al unirse.

**Clasificación:** nombres e identificadores únicos → **Viable**.

### 3.4 Asignación de mandos

- **[V]** MCC lee los mandos con **XInput**. AlphaRing intercepta `get_input_state` y reparte los mandos XInput 0–3
  y el teclado/ratón entre los jugadores locales.
- **[O]** **XInput de escritorio admite como máximo 4 mandos** (`XUSER_MAX_COUNT = 4`). Solo la versión GDK o
  GameInput sube a 8, y MCC no la usa.
- Para 8 mandos en un PC hace falta leerlos **por otra vía**: Raw Input/HID (mandos DualShock/DualSense y
  genéricos), DirectInput, o SDL (que abstrae XInput + HID). Después hay que entregar a cada instancia y a cada
  jugador local solo "sus" mandos. Esto sustituye la lectura de MCC por la nuestra en `get_input_state`, como ya
  hace AlphaRing.
- Problema práctico: los mandos Xbox por XInput siguen limitados a 4 en todo el sistema. Con 8 mandos Xbox
  habría que leer los extra por HID o por Windows.Gaming.Input. El límite de este último **no está confirmado**.

**Clasificación:**
- 4 mandos por instancia → **Viable**.
- 8 mandos independientes en un PC → **Posiblemente viable** (depende del tipo de mando; hay que medirlo con el hardware real).

### 3.5 Cámaras y pantalla dividida

- **[V]** Cada usuario local tiene su observador (cámara) y su `c_player_view`. Reach elige el rectángulo de cada
  vista de una tabla de 5×4 (sin jugadores / 1 / 2 / 3 / 4).
- **[V]** AlphaRing aplica en Reach un parche "Remove Black Bar", lo que confirma que la tabla de vistas de Reach
  se puede modificar.
- **8 vistas en una instancia:** no viable (§3.2).
- **8 vistas en un monitor con 2 instancias:** cada instancia dibuja 4 vistas en su ventana sin bordes, y las dos
  ventanas se colocan juntas:

```
 Monitor 16:9 (p. ej. 3840×2160)         Alternativa: 2 monitores
 ┌────┬────┬────┬────┐                   ┌─────────┬─────────┐   ┌─────────┬─────────┐
 │ P1 │ P2 │ P5 │ P6 │  ← instancia A     │   P1    │   P2    │   │   P5    │   P6    │
 ├────┼────┼────┼────┤    (izquierda)     ├─────────┼─────────┤   ├─────────┼─────────┤
 │ P3 │ P4 │ P7 │ P8 │  ← instancia B     │   P3    │   P4    │   │   P7    │   P8    │
 └────┴────┴────┴────┘    (derecha)       └─────────┴─────────┘   └─────────┴─────────┘
 cada vista ≈ 960×1080 (8:9)              cada vista 16:9 normal
```

- **Diseños de 6 jugadores:** 2 instancias × 3 vistas, o 4 + 2.
- **Rendimiento:** son 8 renders completos de la escena por fotograma, en dos procesos. Habrá que medirlo. En la
  Xbox 360, Reach ya reducía el detalle en pantalla dividida. La memoria y la GPU de dos MCC simultáneos son el
  riesgo principal de este modo.

**Clasificación:**
- 4 vistas → **Viable**.
- 6/8 vistas en una instancia → **No viable**.
- 6/8 vistas con 2 instancias → **Posiblemente viable** (pendiente de rendimiento).

### 3.6 Cooperativo en red (base del Modo B y también del híbrido A)

- **[O/C]** Reach usa una red de **simulación distribuida con autoridad del host**: replicación no fiable al estilo
  Tribes (David Aldridge, GDC 2011). **No es lockstep.** Cada jugador extra añade ancho de banda y entidades
  replicadas, pero no obliga a que todos simulen igual de forma determinista.
- **[C]** En julio de 2023, MCC subió el **Tiroteo personalizado de Reach y ODST de 4 a 8 jugadores**, junto con la
  entrada a partidas en curso. Es la evidencia más fuerte del proyecto: **el motor de Reach ya ejecuta oficialmente
  partidas con IA, oleadas y 8 humanos en red.**
- Matiz: el Tiroteo usa el motor *survival*, un tipo de motor multijugador. La campaña usa el modo `campaign`, con
  scripts de misión, puntos de control, cinemáticas y sus propias reglas de sesión. Que funcione en Tiroteo
  **no garantiza** que funcione en campaña.
- **[V]** La rama `master` de AlphaRing anuncia *"8 Players Campaign / Splitscreen 8 players"* en su README.
  **No he podido ver la captura** (github.com está bloqueado en este entorno) ni saber qué juego es o si se hizo
  con 2 equipos × 4 jugadores. El selector de jugadores locales del código está limitado a 1–4, así que lo más
  probable es que fueran varios equipos o instancias en red. **Pendiente de confirmar con el autor o su Discord.**
  Si se confirma, reduce mucho el riesgo.

**Clasificación:**
- Sesión de campaña con 5–8 jugadores en red → **Posiblemente viable** (es el corazón del proyecto).
- Sincronización (posiciones, disparos, vehículos, IA, objetivos) → **Posiblemente viable**. La resuelve el propio
  motor, no la escribiríamos nosotros. El riesgo está en el ancho de banda y en el reparto del host con 8 jugadores.

### 3.7 Limitaciones de la campaña (riesgo de contenido)

Aunque se suba el límite de la sesión, la **campaña se diseñó para 1–4 jugadores**:

| Riesgo | Qué puede pasar con los jugadores 5–8 | Mitigación posible | Estado |
|---|---|---|---|
| Puntos de inicio (`player starting locations`) | Si el mapa solo define 4, los jugadores 5–8 pueden no aparecer o aparecer encima de otros | Reutilizar los existentes con desplazamiento, o crear puntos nuevos con las Mod Tools | Pendiente |
| Funciones de script `player0`…`player3` | Los scripts que mueven o comprueban jugadores concretos ignoran a 5–8 | Hacer que 5–8 "sigan" a su compañero 1–4 (teletransporte en cinemáticas); en mapas recompilados, editar scripts | Pendiente |
| Cinemáticas y teletransportes | Jugadores 5–8 abandonados fuera de zona, muertos por *kill volumes* o bloqueando una secuencia | Teletransporte al jugador 0 al terminar la cinemática | Pendiente |
| Vehículos de guion (Sabre en *Long Night of Solace*, Falcon en *New Alexandria*) | Los scripts crean vehículos por jugador o tienen asientos limitados | Analizar misión por misión | **Riesgo alto** |
| Reaparición cooperativa | Mismo sistema que con 4; debería escalar | Verificar | Pendiente |
| Puntos de control / guardado | El *game state* guarda la tabla de jugadores; 8 entra en 16 | Verificar | Pendiente |
| Escalado de dificultad / IA | Funciona igual, pero con 8 humanos la campaña será más fácil | Calaveras (Hierro, Famine…) | Viable |
| Películas (*Theater*) | Puede fallar al reproducir partidas de 8 | Fuera de alcance inicial | Pendiente |

Las **Mod Tools oficiales** permiten recompilar los mapas de campaña con más puntos de inicio y scripts adaptados.
Ese es el camino "limpio" para lo que no se pueda arreglar en tiempo de ejecución, sin parchear el motor.

**Clasificación:** campaña completa estable con 8 → **Pendiente de investigación** (por misión; hay 10 + epílogo).

### 3.8 Posibilidad de modificar estos sistemas

| Sistema | Técnica | Viabilidad |
|---|---|---|
| Límite de sesión de campaña | Parche de bytes o *hook* en `haloreach.dll` + lobby de MCC | Posiblemente viable |
| Usuarios locales (1–4) | *Hook* de `i_game_manager` (como AlphaRing) | Viable |
| Usuarios locales (5–8, una instancia) | Reubicar matrices compiladas | **No viable** |
| Entrada de 8 mandos | Capa propia (SDL / Raw Input) dentro del *hook* de entrada | Posiblemente viable |
| Spawn y scripts | En tiempo de ejecución (HaloScript en vivo, AlphaRing ya lo hace en H3) o mapas recompilados con Mod Tools | Pendiente |
| 2 instancias de MCC en un PC | Ventanas, aislamiento de entrada y que Steam y los servicios acepten dos sesiones | **Pendiente**. Nucleus Co-op lo afirma para MCC [C], pero necesita dos cuentas con licencia y **no** usaremos emuladores de Steam para saltarse la propiedad |
| Antitrampas | **No se toca.** Modo oficial EAC desactivado | — |

### 3.9 Pantalla dividida frente a LAN

| | Modo A (un PC) | Modo B (varios PCs) |
|---|---|---|
| Lo que exige | 2 instancias × 4 locales **+ todo lo del Modo B** | Subir el límite de sesión a 8 |
| Mandos | 8 en un PC (límite XInput) | Cada PC los suyos |
| Rendimiento | 8 vistas en una GPU | Normal en cada PC |
| Dependencias | Ejecutar 2 MCC a la vez (sin confirmar) | Un MCC por PC |
| Complejidad | **Alta** | **Media** |
| Variantes | — | 8 PCs × 1, **2 PCs × 4** (AlphaRing en cada uno), 4 × 2… |

**Conclusión: el Modo A no es una alternativa al B. El Modo A *contiene* al B.** Con cualquier configuración, los
8 jugadores tienen que estar en **una sola sesión cooperativa de red**, porque una instancia no admite más de 4.
Por eso el orden lógico es: primero B, después A.

Sobre la "LAN": no se conoce un modo *System Link* ni LAN sin Internet en MCC para PC. Las partidas
cooperativas usan las invitaciones de Steam o Xbox y pasan por el transporte de red de MCC. Una LAN real sin
Internet obligaría a sustituir ese transporte (`network_sendto_*`). **Pendiente de investigación; fuera del
alcance inicial.** En la práctica, "LAN" será una partida personalizada online entre PCs de la misma casa.

---

## 4. Tabla resumen de clasificación

| # | Característica | Clasificación |
|---|---|---|
| 1 | Arquitectura de Reach en MCC entendida y enganchable | **Viable** |
| 2 | Registro de los jugadores 5–8 en la sesión | **Posiblemente viable** |
| 3 | Límite de 4 jugadores locales por instancia (hasta 4) | **Viable** |
| 3b | 5–8 jugadores locales en una instancia | **No viable con las herramientas actuales** |
| 4 | Identificadores y nombres únicos | **Viable** |
| 5 | 4 mandos por instancia | **Viable** |
| 5b | 8 mandos independientes en un PC | **Posiblemente viable** |
| 6 | 4 vistas de pantalla dividida | **Viable** |
| 6b | 8 vistas en una instancia | **No viable con las herramientas actuales** |
| 6c | 6/8 vistas con 2 instancias en un monitor | **Posiblemente viable** |
| 7 | Campaña completa estable con 8 (spawn, scripts, cinemáticas, vehículos) | **Pendiente de investigación** |
| 8a | Sesión de campaña de 8 en red (Modo B) | **Posiblemente viable** |
| 8b | Sincronización de IA, objetivos y vehículos con 8 | **Posiblemente viable** |
| 8c | 2 instancias de MCC en un PC | **Pendiente de investigación** |
| 8d | LAN real sin Internet | **Pendiente de investigación** |
| 8e | Rendimiento de 8 vistas en un PC | **Pendiente de investigación** |
| — | Modificar o evadir EAC | **Descartado por regla del proyecto** |

---

## 5. Arquitectura propuesta (pendiente de aprobación)

### 5.1 Principios

1. **Base sobre AlphaRing + libmcc** (licencia MIT). Ya resuelven la carga, los *hooks*, la pantalla dividida de
   hasta 4 y los perfiles. Reescribir eso desde cero solo añade errores. Lo propio (sesión de 8, spawn, entrada de
   8 mandos, diagnóstico, instancias) irá en **módulos separados**, como *fork* o como complemento.
2. **Nada de *offsets* fijos a ciegas.** Se buscarán por patrón de bytes (*signature scanning*) y se validarán con
   la versión del ejecutable. **Si la versión es desconocida o falla una firma, el mod se desactiva solo** y el
   juego sigue sin tocar.
3. **Archivos originales intactos.** El mod es un DLL proxy en `mcc\binaries\win64`. No se modifica ningún
   archivo del juego. Si hay mapas recompilados, irán por Steam Workshop o por una carpeta propia, con copia de
   seguridad automática y desinstalador.
4. **EAC siempre en modo oficial "desactivado".** Nunca se intercepta ni se emula.

### 5.2 Módulos

```
halo-reach-8p/
├─ docs/                    Informe, registro de offsets, notas por misión
├─ src/
│  ├─ loader/               DLL proxy (WTSAPI32), detección Steam/WinStore y versión
│  ├─ core/                 Escaneo de firmas, gestor de hooks, configuración (r8.ini)
│  ├─ session/  ◄ CRÍTICO   Límite de campaña 4→8 (lobby MCC + COOP_JOIN/limit en haloreach.dll), XUID únicos
│  ├─ campaign/             Spawn de 5–8, seguimiento en cinemáticas, reaparición, parches por misión
│  ├─ input/                SDL/RawInput → asignación mando↔jugador↔instancia, perfiles de botones
│  ├─ layout/               Colocación de ventanas de 2 instancias, diseños 4/6/8
│  └─ diag/                 Log, overlay (tabla de jugadores, XUID, peer, unidad, ping), minidump, watchdog
├─ tools/
│  ├─ sigscan/              Analizador offline de haloreach.dll (para cada versión de MCC)
│  └─ install/              Instalador con copia de seguridad y desinstalador
└─ README.md                En español
```

### 5.3 Fases con puntos de decisión

| Fase | Objetivo | Prueba de aceptación | Si falla… |
|---|---|---|---|
| **F1** Análisis estático | Localizar en **tu** `haloreach.dll` las funciones de unión cooperativa, el límite de campaña y la tabla `players` | Lista de firmas validada para tu versión | Re-evaluar |
| **F2** Base 4 jugadores | Compilar AlphaRing actual + módulo `diag`; campaña de Reach con 4 locales | *Winter Contingency* completa con 4 | Arreglar antes de seguir |
| **F3** 5.º jugador | 2 PCs: PC-A con 4 locales + PC-B con 1, misma sesión de campaña | El 5.º jugador aparece, dispara y conduce | **Punto de no retorno:** si el motor no admite el 5.º, el proyecto de 8 en campaña **no es viable** y se propone la alternativa (Tiroteo de 8 en mapas de campaña) |
| **F4** 6–8 jugadores | 2 PCs × 4 | Cámaras, mandos y sincronización estables 30 min | Limitar a 6 |
| **F5** Campaña | Misión por misión; parches de spawn y cinemáticas | Las 11 misiones terminables | Lista de misiones no soportadas |
| **F6** Un solo PC | 2 instancias × 4 + `input` de 8 mandos + `layout` | 8 mandos y 8 vistas en un monitor | Modo B como solución final |

### 5.4 Lo que necesito de ti para empezar

1. **Aprobación** de la arquitectura y del orden (red primero, PC único al final).
2. **Versión exacta de MCC** (Steam): propiedades de `MCC-Win64-Shipping.exe` → Detalles.
3. Para F1: ejecutarás en tu PC una herramienta de análisis que genero yo (solo lectura de `haloreach.dll`, sin
   subir el binario a ningún sitio). Te devolverá un informe de firmas. No puedo ejecutar el juego desde aquí.
4. Para F3/F4: **2 PCs con MCC** (o, si no, confirmar si tienes 2 cuentas de Steam con MCC para probar 2
   instancias en un PC).
5. Qué **mandos** tienes (Xbox, PlayStation, genéricos) y cuántos de cada tipo.
6. Si te parece bien que el proyecto viva en este repositorio (`DarthRec`, que hoy es una app Android) o prefieres
   un repositorio nuevo.

---

## 6. Referencias

- AlphaRing — <https://github.com/WinterSquire/AlphaRing> (MIT). Ramas `dev` y `master` revisadas.
- libmcc — <https://github.com/SpringContingency/libmcc>
- Halo Support: lanzar MCC con EAC desactivado — <https://support.halowaypoint.com/hc/en-us/articles/360037475251>
- Halo Support: mods vía Steam Workshop — <https://support.halowaypoint.com/hc/en-us/articles/28916975982868>
- Halo Waypoint: herramientas de Reach (sept. 2022) — <https://www.halowaypoint.com/news/halo-mcc-august-update-is-live>
- Halo Support: actualización de julio de 2023 (Tiroteo de 8 jugadores) — <https://support.halowaypoint.com/hc/en-us/articles/17144473706900>
- Purexbox: cambios en Tiroteo, julio de 2023 — <https://www.purexbox.com/news/2023/07/halo-master-chief-collection-july-update-includes-huge-changes-to-firefight-mode>
- Microsoft Learn: XInput (máximo 4 mandos) — <https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput>
- Microsoft GDK: XInput sobre GameInput (`XUSER_MAX_COUNT` 4→8) — <https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/input/porting/input-porting-xinput>
- Co-optimus: límites cooperativos de MCC y Reach — <https://www.co-optimus.com/game/7885/xbox-series/halo-the-master-chief-collection.html>
- GDC 2011, D. Aldridge, *I Shot You First: Networking the Gameplay of Halo: Reach* — <https://www.gamedeveloper.com/business/gdc-vault-adds-free-crawford-i-halo-reach-i-maxis-lectures>
- Nucleus Co-op (MCC) — <https://nucleus-co-op.pages.dev/games/halo-mcc/> (no accesible desde este entorno)

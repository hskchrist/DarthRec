# Plan de pruebas de Reach8

Las pruebas se hacen en tu PC con el juego real: Reach8 se escribió sin acceso a MCC.
Después de cada prueba, guarda el informe (F8 → Diagnóstico → *Guardar informe*) y
el registro de `Documentos\Reach8\logs\`.

Misión de prueba inicial: **Winter Contingency** (la primera, sencilla y con vehículos).
Dificultad: Normal. Empieza siempre desde el **inicio de la misión**, no desde un punto de control.

## Prueba 0 — El mod se carga sin romper nada

1. Instala con `install.ps1` e inicia MCC con *Anti-Cheat Disabled*.
2. Esperado: aparece arriba a la izquierda "Reach8 cargado - F8…".
3. Pulsa F8: se abre el menú. En **Diagnóstico** debe poner:
   - `Ganchos MCC: activos`
   - `Perfil de build: …` (si pone *desconocida*, ejecuta `r8scan.py` y envía el informe)
4. Cierra el menú y juega una misión **con 1 jugador**: debe comportarse igual que sin el mod.

| Resultado | Qué significa |
|---|---|
| No aparece el aviso ni se abre el menú | El DLL no se cargó o falló al iniciar: mira el registro |
| `Ganchos MCC: INACTIVOS` | MCC cambió: envía `r8scan_informe.txt` |
| El juego se cierra al arrancar | Desinstala, envía el registro y el volcado de `crash\` |

## Prueba 1 — 2 jugadores

1. F8 → Jugadores → 2. Jugador 1: Teclado/ratón. Jugador 2: *Asignar mando* y pulsa A.
2. Inicia Winter Contingency.
3. Comprobar:
   - [ ] La pantalla se divide en 2.
   - [ ] Cada jugador mueve solo a su Spartan.
   - [ ] El jugador 2 muestra su nombre.
   - [ ] Vibración solo en el mando del jugador 2.
   - [ ] Conducir el Warthog: uno conduce y el otro va de artillero.
   - [ ] Morir y reaparecer junto al compañero.
   - [ ] Llegar a un punto de control y cargarlo (Pausa → Volver al punto de control).

## Prueba 2 — 4 jugadores (comprobar que todo sigue funcionando)

Igual que la prueba 1, con 4 mandos (o teclado + 3 mandos).
- [ ] 4 vistas, sin vistas negras ni duplicadas.
- [ ] Ningún mando controla a dos jugadores.
- [ ] Desconectar y reconectar un mando: vuelve a su jugador (si estaba asignado con *Asignar mando*).
- [ ] Rendimiento aceptable (anota los FPS).

## Prueba 3 — 2 ventanas del juego en el mismo PC (base para 5–8)

1. Abre la segunda ventana (`MCC-Win64-Shipping.exe` desde `mcc\binaries\win64`).
2. Anota qué pasa: ¿se abren las dos? ¿Steam cierra alguna?
3. Si se abren: F8 en cada una → Ventana → izquierda / derecha. Asigna mandos.
4. Comprobar:
   - [ ] La pestaña **Mandos** muestra qué ventana tiene cada mando.
   - [ ] Un mando de la ventana 1 no mueve nada en la ventana 2.
   - [ ] Las dos ventanas aguantan 10 minutos jugando (cada una su propia partida).

## Pruebas 4–7 — 5, 6, 7 y 8 jugadores en la misma campaña

**Bloqueadas** hasta que exista el puente de red entre ventanas (ver LIMITACIONES).
Cuando exista, el orden será:

| Jugadores | Objetivo |
|---|---|
| 5 | Ventana 1 con 4 + ventana 2 con 1: crear al 5.º jugador. **Prueba decisiva** |
| 6 | Cámaras y controles de 2 jugadores en la ventana 2 |
| 7 | Sincronización: disparos, vehículos e IA iguales en las dos ventanas |
| 8 | Campaña completa, misión por misión |

Para ayudar a preparar esa fase, activa `netprobe = 1` en `reach8.ini` y juega una
partida cooperativa normal por Internet con un amigo (si puedes). El informe mostrará
cuánto tráfico mueve el motor.

## Misiones después de Winter Contingency

ONI: Sword Base → Nightfall → Tip of the Spear → Long Night of Solace → Exodus →
New Alexandria → The Package → The Pillar of Autumn → Lone Wolf.
Anota en cada una: fallos, cierres, jugadores atascados tras cinemáticas, vehículos.

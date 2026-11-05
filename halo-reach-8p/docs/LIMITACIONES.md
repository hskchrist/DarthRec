# Registro de errores y limitaciones conocidas — Reach8 0.1

## Limitaciones del diseño

1. **Máximo 4 jugadores por ventana del juego.** Es un límite del motor de Reach
   (matrices de tamaño fijo para cámaras, entrada, HUD y pantalla dividida). No se
   puede subir cambiando un número. Ver `INFORME_TECNICO_VIABILIDAD.md` §3.2.
2. **5–8 jugadores en la misma campaña: no implementado.** Requiere unir 2 ventanas
   del juego por red. Con una sola cuenta de Steam no se puede usar la red de Steam
   entre ellas, así que haría falta un puente de red propio. Falta investigación
   (ver la fase de red en el informe) y puede resultar imposible.
3. **Menú "traer / ocultar jugadores": no implementado.** Necesita localizar en
   memoria la tabla de objetos y jugadores de Reach para la versión actual de MCC.
4. **Los jugadores 2–4 comparten el perfil del jugador 1** (sensibilidad, botones,
   armadura). MCC solo tiene un perfil por cuenta.
5. **Sin LAN real.** MCC en PC no tiene red local sin Internet.
6. **Logros, XP y matchmaking no funcionan** con *Anti-Cheat Disabled* (decisión
   de MCC, no del mod).

## Riesgos técnicos (sin probar todavía)

| # | Riesgo | Qué pasaría | Qué hace Reach8 |
|---|---|---|---|
| R1 | MCC se actualizó después de marzo de 2025 | Los parches de `haloreach.dll` no encajan; puede que el motor no deje entrar a los jugadores 2–4 | No aplica ningún parche y lo indica en Diagnóstico. Solución: `r8scan.py` + `parches_propios` |
| R2 | La firma de la interfaz de MCC dejó de existir | El mod no se engancha | Se desactiva solo; el juego funciona normal |
| R3 | Los índices de la tabla de MCC cambiaron | Una función equivocada recibe el gancho | Se valida que cada entrada apunte a código de MCC; si algo falla, se deshace todo. Un cambio de orden no se detecta: si ves comportamientos raros, desinstala y envía el registro |
| R4 | En los menús de MCC, un mando ajeno toma el control del jugador 1 | Molesto, no grave | AlphaRing lo evita con un gancho cuya dirección no tiene firma pública; Reach8 0.1 no lo incluye |
| R5 | Más de 4 mandos Xbox | Windows (XInput) solo da 4; los demás dependen de Windows.Gaming.Input vía SDL3 | Pendiente de probar. Los mandos PlayStation y Switch van por HID, sin ese límite |
| R6 | Puntos de aparición de los jugadores 3 y 4 | La comunidad informa de apariciones raras con AlphaRing | Sin solución en 0.1 |
| R7 | Abrir 2 MCC a la vez con una sola cuenta | Steam puede impedirlo | Pendiente de tu prueba |
| R8 | `netprobe` con índices de red equivocados | Podría romper la red | Apagado por defecto; solo para investigación |
| R9 | El estado del mando con la cruceta | Si los bits de botones no siguen el formato XInput, los botones saldrían cambiados | Reach8 usa el mismo formato que AlphaRing, que funciona en el juego |

## Errores encontrados

*(Vacío: aún no hay pruebas en el juego real. Añade aquí cada error con la fecha,
la versión de MCC, la misión, los pasos para reproducirlo y el registro.)*

| Fecha | Versión MCC | Misión | Descripción | Estado |
|---|---|---|---|---|
| | | | | |

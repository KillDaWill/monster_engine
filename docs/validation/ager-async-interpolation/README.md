# Interpolación asíncrona del ager

La demo usa el resultado de `MonsterAger` como autoridad anatómica. La
publicación del worker conserva cuerpo, colores, ojos y mandíbula del mismo
snapshot, también en MORPH. El skinning de una malla anterior no puede crear
superficie que no existía en su topología y ya no sustituye la metamorfosis.

Durante movimiento continuo el worker termina el snapshot en curso y conserva
la solicitud pendiente más reciente. La demo avanza sólo después de publicar
su objetivo, con incrementos máximos de 0,005. No acumula el tiempo perdido en
reconstrucciones ni depende de la escala para esperar los extremos. Este
control de reproducción es independiente de los tipos de criatura; no añade
nuevas correspondencias anatómicas a MonsterAger para especies arbitrarias.

## Límite de rendimiento

Se conserva la resolución anatómica de los tres tiers. La velocidad de la
metamorfosis se adapta al mallador: la ventana puede dibujar a 60 FPS mientras
la anatomía cambia a la frecuencia de reconstrucción. Esto elimina la omisión
de fases, pero no constituye una interpolación de superficie a 60 FPS ni
garantiza ausencia matemática de diferencias entre triangulaciones sucesivas.

## Verificación

- `make -B -j4 test`: todas las suites pasan, incluidas las regresiones de
  snapshot MORPH y el crecimiento de ojos/mandíbula tras publicación.
- Capturas OpenGL de edad 0,5 en MORPH y SETTLED: inspección lateral, frontal y
  dorsal en `multiview.png`; ambas calidades conservan la fase intermedia.
- El validador de ciclo comprueba aparición de dedos, cobertura de cada décima
  en ambos sentidos y paso máximo entre edades publicadas.

Recorrido OpenGL completo: **406 mallas, 0 fallos, 0 solicitudes coalescidas**.
El análisis de la escala publicada comprueba cobertura 0–1–0 y paso máximo
0.005063 (incluye redondeo de la traza). `cycle.png` compara ida y vuelta.
La comprobación del paso y cobertura se ejecutó sobre `cycle.log`; las mismas
comprobaciones se añadieron después al validador de la demo.

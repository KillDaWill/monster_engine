# Metamorfosis entre recetas

## Implementación

`CreatureMorph_Interpolate` interpola anatomía resuelta utilizando IDs semánticos estables. Construye la unión de nodos y conexiones de ambos extremos; una rama ausente se repliega hacia su ancestro compartido y reduce su desarrollo. Los dedos interpolan dirección y longitud por separado. El perfil axial conserva las estaciones de ambos extremos y alinea la pelvis para que el crecimiento de la cola no estire el tronco.

La cabeza interpola su receta de superficie, landmarks, boca y ojos. El cuello y las estaciones craneales se resuelven en `HeadAnatomy_Resolve`, antes de construir el SDF. Las orejas ausentes se contraen en su inserción: se transporta la raíz y se reconstruye el centro con la semialtura nueva, evitando pabellones flotantes. El hash de geometría incluye la superficie cefálica resuelta mediante campos explícitos.

La deformación utiliza cuatro conexiones con pesos que llegan a cero en el límite de soporte. Los enlaces auxiliares sin volumen no atraen vértices de las extremidades. Las instantáneas de metamorfosis se construyen desde la anatomía solicitada; no se reconstruyen desde una receta adulta que cambia al 50%.

`MonsterAger_AdvanceWithGeometry` adapta el avance al desfase respecto a la geometría publicada. La demo limita ese desfase a un intervalo geométrico (0,05). El hilo visual sigue dibujando mientras trabaja el generador. No se ha reducido la resolución para acelerar el ciclo.

## Reproducción

```bash
make -B -j6 test demos
mkdir -p build/morph-validation
./demos/demo_morph_dog_lizard --validate-cycle=build/morph-validation/final-cycle --frames=15000
./demos/demo_morph_dog_lizard --age=0.5 --view=1 --capture-prefix=build/morph-validation/final-side
./demos/demo_morph_dog_lizard --age=0.5 --view=2 --capture-prefix=build/morph-validation/final-front
./demos/demo_morph_dog_lizard --age=0.5 --view=4 --capture-prefix=build/morph-validation/final-head
```

El ciclo escribe diez capturas y un CSV con edad visual, edad geométrica, generación, vértices y duración de construcción. Las capturas fijas esperan geometría asentada. Los archivos de evidencia quedan en `build/morph-validation/` y no se versionan.

## Alcance

La correspondencia requiere IDs compartidos con el mismo significado anatómico, dentro de la capacidad del grafo. No se deduce automáticamente la homología de IDs arbitrarios. El perfil axial explícito cubre cadenas longitudinales; las anatomías ramificadas conservan el camino de conexiones del SDF. La validación visual de esta entrega corresponde a perro ↔ lagarto; no certifica todas las combinaciones de cabezas, número de ojos o planes corporales. Los cambios entre primitivas cefálicas diferentes (por ejemplo, un pico y un cráneo barrido) necesitan una representación continua adicional.

La deformación entre reconstrucciones aproxima la superficie SDF; no garantiza coincidencia exacta al sustituir una malla. La velocidad automática depende del coste de construcción y puede ser mucho menor que la velocidad nominal. El trabajo evita extrapolaciones grandes; no convierte un generador de varios segundos en uno instantáneo.

## Resultados de esta ejecución (24-09-2026)

- `make -B -j6 test demos`: código de salida 0; todas las suites pasan, sin avisos de compilación. `git diff --check` no detecta errores.
- Regresión de continuidad: 101 edades en ambos sentidos, grafos válidos y topología estable. Paso nodal máximo: 0,127079; paso cefálico: 0,006959 por incremento de edad 0,01.
- Cruce del 50%: diferencia máxima del campo 0,025153 con intervalo 0,0002 y 0,002556 con intervalo 0,00002; converge al reducir el intervalo. En la banda superficial muestreada, diferencia máxima 0,001736. No es una cota analítica de todo el dominio.
- Regresión de pesos: vértices vecinos que intercambian el orden de conexiones mantienen continuidad. Se prueban avance, retroceso y rechazo de tiempo no finito del reloj geométrico.
- Ciclo final 0 → 1 → 0: diez capturas, 9578 fotogramas con geometría, 165.78 s; desfase máximo y p95 de 0.05; 41 generaciones al terminar y cero construcciones descartadas. Primera captura próxima a 0, con geometría 0; extremo final exactamente 0. El coste máximo registrado de construcción fue 8003.6 ms. La ejecución coincidió parcialmente con las pruebas y capturas, por lo que no constituye un benchmark aislado.
- Vistas asentadas lateral, frontal y cefálica al 50%: edad geométrica y visual 0,5, 323.970 vértices. Las extremidades conservan una superficie continua en estas vistas; la vista lateral recorta la punta distal de la cola por el encuadre de la demo. El ciclo en tres cuartos permite revisar su silueta completa.

Evidencia local: `final.log`, `final-cycle.log`, `final-cycle.csv`, `final-cycle-00.ppm` a `final-cycle-09.ppm`, `final-views.log` y capturas `final-side`, `final-front`, `final-head` en `build/morph-validation/`.

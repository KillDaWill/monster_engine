# Campos paramétricos de ornamentos

La referencia `reference.png` se utiliza como objetivo visual. Esta entrega amplía
la composición general y deja los perfiles de cabeza/órbita disponibles para
cualquier receta; la equivalencia visual exacta sigue siendo un objetivo de
calibración.

## Controles reutilizables

`OrnamentField` distribuye un `OrnamentArray` sobre una ruta anatómica. Expone filas,
cantidad longitudinal, apertura angular (radianes), intervalo longitudinal,
desfase alterno, envolvente de tamaños y variación determinista por semilla.
La forma individual conserva longitud, radios, curvatura y desarrollo.
`CREATURE_TRAIT_ORNAMENT_FIELD` selecciona anfitrión axial o caudal por clase e
índice, sin depender de IDs concretos. Los anclajes conservan referencias a dos
estaciones y se resuelven de nuevo con los radios actuales del anfitrión.

La sección elíptica admite direcciones intermedias sin el antiguo salto entre
radio dorsal y lateral. La normal de salida sigue la sección. Los cuernos y
espinas tienen conexión estructural al anfitrión y segmentos geométricos propios:
no añaden un cono desde el centro del cráneo o torso. Su detalle se evalúa en una
región compartida con objetivo local por sección; no consume una región por
espina ni utiliza la categoría de extremidad.

`CREATURE_TRAIT_HEAD_DEEP` controla profundidad cefálica independientemente de la
escala global. Desert Horned combina esos controles con los rasgos existentes:
6 cuernos, 18 espinas laterales, 24 dorsales y 6 caudales. Las escamas siguen siendo
procedimentales en el material. No hay un generador especializado para esta variedad.

`CREATURE_TRAIT_HEAD_FORM` compone el volumen craneal (`STANDARD`, `SHIELD`,
`BLOCK`, `NARROW` o `WEDGE`) y el perfil del hocico (`TAPERED`, `BLUNT`, `LONG` o
`WEDGE`). `CREATURE_TRAIT_EYE_LAYOUT` selecciona órbitas laterales, frontales,
altas o bajas; cada disposición modifica posición, orientación y tamaño del globo
manteniendo la copa orbital válida. Estos rasgos son datos de fenotipo y se pueden
combinar con cualquier arquetipo sin introducir IDs de especie.

## Verificación realizada

- `make -B -j4 test demos`: todas las suites pasan; sin advertencias del compilador.
- Pruebas del campo en tronco y cola: determinismo, cambio de semilla, conexión de
  soporte, capacidad y rechazo de apertura no finita sin publicación parcial.
- Construcción y captura de las diez variedades, y capturas lateral, oblicua y
  dorsal monocroma del espécimen 2.
- Comprobación adicional con semilla 731.

Las capturas corresponden a la ruta de malla, no a una validación visual del
raymarcher GPU. No se ha perfilado animación ni crecimiento con los campos nuevos.

## Adaptación de forma corporal (Phrynosoma / Desert Horned)

Se ha ajustado la morfología axial y proporciones del espécimen 2 (`Desert Horned`) para acercarla a `reference.png`:
- Tronco discoidal y achatado: relación de aspecto $L/W \approx 1.03$ (en vez de ~1.48) y $H/W \approx 0.30$.
- Abdomen y tórax posterior como punto de máxima anchura (formando el contorno redondeado tipo "panqueque" característico de *Phrynosoma*).
- Franja lateral de espinas proyectadas en el perímetro expandido del disco corporal.
- Cuello compacto y ancho ($L \approx 0.73$, $W \approx 0.94$) integrando la cabeza con los hombros.
- Extremidades robustas y bajas para soportar el cuerpo extendido.
- Cola corta ($L \approx 2.35$), ancha en la base y ahusada rápidamente.
- La malla generada con 350000 celdas conserva una componente principal de ~238000
  triángulos; los soportes de los módulos ornamentales se incluyen en la unión para
  evitar que las espinas laterales queden como piezas aisladas.

## Reproducir

```sh
./demos/demo_lizard_diversity --specimen=2
./demos/demo_lizard_diversity --specimen=2 --frames=1 --top --monochrome --save-ppm=/tmp/horned-top.ppm
./demos/demo_lizard_diversity --specimen=2 --frames=1 --side --orbit=2.3 --save-ppm=/tmp/horned-side.ppm
```

Capturas: `top.png`, `side.png`, `oblique.png`, `seed-731.png` y `grid.png`.

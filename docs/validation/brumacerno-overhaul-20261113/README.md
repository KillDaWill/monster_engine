# Brumacerno: estudio anatómico del 13-11-2026

## Método

Base: rama `ager-index-demo-20261112`, HEAD inicial `0eb37d7`, con cambios
locales preexistentes en demos y en la indexación de `MonsterSDF_Build`.
Esos cambios no pertenecen a esta intervención y se conservaron.
La referencia visual es el Hereford de `brumacero_out/imagenes/05_referencia/`,
no el bisonte. Se consultó además [UGA B1556](https://fieldreport.caes.uga.edu/publications/B1556/visual-evaluation-of-beef-cattle-for-breeding/).

Las imágenes diagnósticas proceden de geometría real OpenGL, sin pelo ni
pigmentación de piel. Las comparaciones corporales usan la misma cámara
ortográfica fija en coordenadas del actor, pose cerrada, luz, resolución
1200×900 y escala de escena. Los detalles cefálicos mantienen el mismo
desplazamiento y escala respecto al origen del cráneo: su objetivo sigue la
cabeza si cambia la inserción cervical, no es una cámara mundial inmutable.
No se utiliza generación de imágenes ni retoque de siluetas.

El modo `study` de `capture_bovine_anatomy` deja margen para no recortar morro,
cuernos o grupa, y evita remallar por separado cada pezuña durante los ensayos.
El modo original conserva los diagnósticos de mandíbula y pies. Un sexto
argumento permite una edad normalizada usando `CreatureRecipe_EvaluateDevelopment`,
sin excepciones por etapa en el renderizador:

```sh
make build/capture_bovine_anatomy build/inspect_brumacerno
./build/capture_bovine_anatomy recipes/brumacerno.creature.json build/estudio 3 study
./build/capture_bovine_anatomy recipes/brumacerno.creature.json build/intermedio 3 study 0.5
```

Crear previamente los directorios de salida. Los originales, recetas de cada
ensayo, binarios de referencia y logs se guardan en `build/brumacerno-overhaul/`.
Los binarios conservados incluyen el estado local inicial, no sólo HEAD.

## Iteraciones observadas

1. **Base y barrido aislado.** Capturas frontal, lateral, posterior, superior,
   tres cuartos y detalles. Reducir únicamente `muzzleLength` 10/15/20 %
   acorta la cara, pero conserva la pinza nasal, el disco y el labio aislado.
   No basta como solución.
2. **Cabeza ancha y mandíbula independiente, descartada parcialmente.**
   Envolvente longitudinal más corta, frente más ancha y puente menos pinzado.
   Reutilizar sin más el perfil mandibular équido produjo un raíl visible
   bajo las mejillas. Esa versión, con el planum más enterrado, presentó además
   un islote de ocho triángulos en NEAR; no se aisló su causa exacta.
   No se borraron triángulos ni se relajó la prueba
   de componente única.
3. **Volumen anterior y tronco.** Cuello +9,8 %, hombros +7,1 %, tórax
   +7,7 % de anchura y +4,6 % de profundidad; tronco −8,8 %. El perfil
   distribuye la profundidad hacia las costillas posteriores y recoge el
   flanco. La pelvis gana anchura sin escalar toda la criatura.
   El segundo ensayo nasal aún mostraba un borde plano: se corrigió otra vez.
4. **Integración.** Se descartó el perfil mandibular independiente bóvido;
   el recorte posterior del sistema existente conserva más cuerpo mandibular.
   El planum gana profundidad y solapa la premaxila, con narinas exteriores.
   Las patas aumentan raíz y segmentos proximales/medios, conservando los
   controles de grosor distal y la definición de pezuñas. Las longitudes se
   compensan por el descenso exacto de sus anclajes:
   no se acortan arbitrariamente. Orejas menos cóncavas y más laterales.
5. **Corrección de excesos.** La primera raíz muscular gruesa sobresalía como
   una esfera. Reducirla ligeramente e insertar anatómicamente hombro/cadera
   dentro de sus anfitriones conserva el aumento del segmento medio sin esos
   bultos. La envolvente transversal adulta se moderó de 1,38 a 1,28.
6. **Crías.** Las primeras imágenes multietapa revelaron una frente enorme,
   ojos cerrados y planum sin narinas. `development.cephalic` no es una mera
   escala: mezcla los radios anfitriones en `HeadModule`, escala los globos
   oculares y atenúa los cutters faciales. En un bóvido nacido la cabeza ya
   es funcional. La receta mantiene este desarrollo en 1 y expresa la
   ontogenia con sus envolventes, cráneo, morro y cuernos por etapa. No se
   modifica esa semántica genérica para lagartos u otras metamorfosis.
7. **La apertura de boca obliga a iterar otra vez.** Aunque la cuarta versión
   pasó C, editor, contactos y GPU, los primeros planos abiertos seguían
   mostrando una barra mandibular recortada. Se ensayó una mandíbula de seis
   estaciones propia de bóvidos: la versión demasiado fina seguía siendo un
   raíl; profundizarla mejoró el ramus pero dejó una pieza excesivamente
   separada. Agrandar sólo los elipsoides de mejilla produjo bultos visibles
   y se descartó. Se conservó una ganacha integrada en el perfil craneal
   continuo existente, no otra primitiva superpuesta, junto con una mandíbula
   bóvida independiente del maxilar. Se moderó también la profundidad nasal
   y se metió la premaxila dentro del planum para quitar el reborde.

La receta usa controles cefálicos distintos en las cuatro etapas: cara más
corta y bóveda relativamente más alta en crías, sin añadir campos al formato.
La interpolación sigue siendo la del motor.

### Controles y efectos

| Control | Capa y efecto |
| --- | --- |
| `headEnvelope.lengthScale`, `skullLength`, `muzzleLength` | Longitud de la caja craneal y proyección facial; no son multiplicadores equivalentes. |
| `skullWidth`, `headEnvelope.widthScale` | Frente y base craneal; también desplazan órbitas, orejas y raíces de cuernos. |
| `rostrumDorsalSlope` | Descenso del puente y línea oral, manteniendo sus landmarks coherentes. |
| `cheekMass`, `masseterMass`, `zygomaticWidth` | Mejilla y ganacha caudales, no inflación uniforme de toda la cabeza. |
| `maxillaryWidth` | Apoyo maxilar lateral resuelto, separado del planum. |
| `noseScale`, `nostrilPosition` | Radios del planum y posición bilateral sobre su superficie curva. |
| `muzzleForm` | En bóvidos no sustituye la composición regional específica; no arregla por sí solo la pinza nasal. |
| `cervicalDorsalMass` | Redistribuye la curva dorsal entre estaciones, sin añadir una joroba intermedia. |
| `axial.profile[]` | Costillas profundas, flanco recogido y pelvis; estaciones y tangentes existentes, sin nuevas primitivas. |
| `rootThicknessScale`, `proximalThicknessScale`, `middleThicknessScale`, `attachmentInset` | Raíz, plenitud proximal, sección media e integración; no se engordan metapodios ni cascos. |

El descenso del anclaje anterior es `3,16×1,02/2 − 3,02×1,02/2 = 0,0714`.
El posterior es `2,48×1,06/2 − 2,34×1,08/2 = 0,0508`.
Se sustraen esas cantidades a las longitudes respectivas, conservando la suela
en Y=0 y dejando al rig resolver los contactos durante la marcha.

## Fuente de verdad y alcance

- `recipes/brumacerno.creature.json`: proporciones y progresión ontogenética.
- `src/Head.c`: resolución bóvida de puente, planum y musculatura. Antes,
  `masseterMass`, `maxillaryWidth` y `zygomaticWidth` tenían efectos nulos
  o muy limitados en esas regiones del arquetipo; ahora gobiernan sus radios
  y posiciones. No hay condición por ID de especie.
- El respaldo C se regenera desde JSON, nunca se edita a mano.
- No se modifican materiales, pigmentación, pelo, articulaciones del casco
  ni evaluadores CPU/GLSL para esconder problemas anatómicos.

## Límite visual pendiente: no confundir contactos correctos con piel convincente

**La aceptación visual completa no está alcanzada.** El cuerpo y las
proporciones mejoran, pero el labio/cuerpo mandibular todavía puede leerse como
una pieza mecánica en primeros planos, sobre todo al abrir la boca. La nariz
ya no es el disco inicial, pero sigue siendo una aproximación elipsoidal, no
un modelado detallado del planum bovino. El tronco es más compacto y profundo,
pero conserva tramos de sección bastante uniforme; las orejas todavía tienen
un borde duro y un aspecto de pabellón genérico. Las imágenes no justifican
llamar al resultado un toro plenamente realista.

El límite que muestran los ensayos es la unión de tejidos: `MonsterVisual.c`
rota rígidamente `jawBase` y deforma `hingeBase` mediante pesos de distancia a
unos pocos anclajes. `MonsterSDF_EvalSeamDistance` construye correas capsulares y
un apoyo gular posterior. **Sí hay tejido de bisagra y deformación**, pero no
un perfil bucal regional que envuelva naturalmente la comisura y el masetero
con estas proporciones. No sería correcto afirmar que el motor carece de toda
costura blanda, ni que pasar las pruebas resuelve este aspecto.

Resolverlo bien requiere intervenir en la geometría, límites, propiedad y
deformación de la costura compartida, contrastando mandíbula cerrada/abierta,
otras especies, interpolación y ambos evaluadores GPU. Se deja explícito en
vez de ocultarlo con pelo, pigmentación, menor apertura o eliminación de
triángulos. Esta entrega no presenta esos cambios de propiedad como una
simple modificación segura de parámetros.

## Estado de verificación

La ejecución final incluye el perfil craneomandibular retenido:

- `make test`: todas las suites pasan. Se añaden 25 edades interpoladas,
  450 puntos de salida nasal, continuidad axial, controles cefálicos efectivos
  y apoyo de las cuatro patas; no se debilita la prueba de componente única.
- `make editor-test`: 10 pruebas, 37,055 s, OK.
- `validate_bovine_body.py --anatomy-only`: adulto, juvenil y larva; captura
  adicional de seed y edades 0,166667 / 0,5 / 0,833333. Todas pasan. Las mallas
  corporales capturadas tienen una componente. NEAR/MID/FAR se comprueban
  también en la suite C y se captura un ciclo LOD en el ecosistema.
- GPU: 5.204 muestras por etapa, los cuatro endpoints. Error máximo
  CPU/compute/fragment **1,073×10⁻⁶ m**; exceso máximo en los ensayos de
  intervalos conservadores **9,686×10⁻⁷ m**.
- Marcha, giro, huida, comida y bebida capturados en sus estados reales.
  Máximo error de landmark oral en los casos Brumacerno: **3,099 mm**.
  Máximo error de pie en apoyo: **1,342 mm**. El máximo incluyendo pies en
  vuelo es 107,695 mm durante huida: no es error de un pie plantado.
- Se repiten además esas cinco acciones con `--initial-age=200`. Atención:
  `capture_entity ... stage=1` imprime `CreatureLifeStage.LIFE_ADULT`, **no**
  `CreatureStage.LARVA`. La anatomía adulta de la receta es el endpoint 3;
  confundir ambas enumeraciones produjo un falso fallo del primer comprobador
  auxiliar, corregido sin cambiar el motor.
- Paridad de skinning CPU/GPU: posición máxima 0 en las muestras del
  validador; normal máxima 2,70×10⁻⁷. También se captura skinning CPU.
- Velkar: comparación de píxeles exacta con el binario inicial, sin diferencias.
  Sus 17 componentes estaban ya en la base; no se atribuyen a esta intervención.
  Esto no equivale a un golden visual de todas las demás especies.
- Generador `--check` y `git diff --check`: sin errores.

### Mediciones resueltas

Son landmarks del motor, no longitudes biológicas equivalentes a una fotografía.
El ancho de morro incluye ahora el planum exterior, no sólo la premaxila interior.

| Etapa | Longitud de cabeza antes → después | Ancho cigomático antes → después |
| --- | --- | --- |
| seed | 0,582295 → 0,512772 | 0,596218 → 0,554440 |
| larva | 1,103491 → 0,901063 | 1,023854 → 0,981909 |
| juvenile | 2,326296 → 1,867931 | 1,858325 → 2,015221 |
| adult | 3,362867 → 2,823300 | 2,554973 → 3,047945 |

Adulto: longitud cefálica −16,0 %, anchura cigomática +19,3 %;
`head_length / withersHeight` aproximadamente 0,764 → 0,641. Cero huecos
en las 374 muestras de continuidad del inspector por etapa. El diagnóstico
adulto fino pasa de 426.456 vértices / 852.916 triángulos a
422.783 / 845.570; no se suprimen islotes para obtener esa reducción.

### Rendimiento emparejado, sin promesa de aceleración

RTX 3050 Laptop, NVIDIA 580.178.04, OpenGL 4.6, 1280×800, 600 cuadros,
semilla 42, 24 herbívoros + 6 carnívoros, sin plantas, vista overview,
zoom 40, sin HUD. Mismo estado local de demos e indexación en A y B;
A usa el binario/recetario inicial conservado, B la implementación final.
Secuencia ABBA, sin otros renders concurrentes:

| Ejecución | Media ms | P50 ms | P95 ms | GPU media ms | Preparación geometría s |
| --- | --- | --- | --- | --- | --- |
| A1 | 21,660 | 20,494 | 26,105 | 7,190 | 14,453 |
| B1 | 21,611 | 20,477 | 25,825 | 7,363 | 17,476 |
| B2 | 22,075 | 21,110 | 26,131 | 7,592 | 17,236 |
| A2 | 20,981 | 19,798 | 25,127 | 7,013 | 14,962 |

Promedio de medias: **21,321 → 21,843 ms**, aproximadamente
**46,90 → 45,78 FPS**, +2,45 % de tiempo por cuadro. GPU +5,29 %,
preparación inicial +18,0 % y RSS máximo medio +3,29 %. Se conserva el
umbral de 30 FPS en este escenario, pero no se obtiene una aceleración.
Son dos muestras por variante; no constituyen una estimación con intervalo
de confianza ni una atribución causal precisa de cada diferencia temporal.
La captura anterior de la cuarta iteración rondó 49,4 FPS; no se reutiliza
como resultado del perfil final ni se fuerza una equivalencia con los
49,5 FPS históricos bajo otras condiciones.

La población comienza en 30 y termina en 26: media dibujada **28,265**,
rango 26–30 en las cuatro ejecuciones. No son 30 animales visibles durante
todos los cuadros. El error plantar agregado de este escenario mixto es
47,226 mm, idéntico en A/B; no confundirlo con los 1,342 mm de las capturas
Brumacerno individuales.

### Evidencia

- [Cuerpo, cinco vistas antes/después](body-before-after.jpg).
- [Cabeza, tres vistas antes/después](head-before-after.jpg).
- [Boca cerrada, parcial y abierta](mouth-before-after.jpg): conserva visible
  el defecto mandibular pendiente; el encuadre es local al rostro.
- [Cuatro etapas y tres edades intermedias](growth.jpg): cada edad se
  encuadra por separado, no representa tamaños absolutos comparables.
- [Acciones y LOD](actions.jpg).
- [Acciones con edad inicial de 200 s](adult-actions.jpg) y sus
  [comandos](adult-commands.json).
- [Superficie antes/después](coat-before-after.jpg), sin alterar pigmentación.
  Mantiene la calibración ecológica derivada de cada receta; no es una
  comparación metrológica a escala mundial fija como la captura neutral.
- [Cuatro pezuñas: lateral y suela](feet.jpg).
- [Barrido inicial de longitud facial](muzzle-experiments.jpg).
- [Ensayo tardío del perfil craneal](cranial-pass5.jpg), no una supuesta
  fotografía final.
- [Mediciones y perfiles numéricos](measurements.json),
  [comandos ejecutados](commands.json), [salidas de validación](validation-results.txt)
  y [huellas de fuentes/binarios](hashes.json).
  El encuadre superior ampliado tiene [comandos propios](top-commands.json)
  y [huellas propias](top-hashes.json): cambia la cámara, no la anatomía.

No se ha reproducido aquí una secuencia temporal completa de metamorfosis
asíncrona con retraso artificial de workers para este último perfil. Las
edades intermedias capturadas y la suite de morph no sustituyen esa evidencia.

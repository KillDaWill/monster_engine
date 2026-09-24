# Rediseño de la cara y los pabellones caninos

Capturas del árbol de trabajo del 24 de septiembre de 2026. Las vistas frontal, lateral izquierda, tres cuartos y superior de `before/` y `after-neutral/` utilizan la misma cámara y campo de visión del demo. Los primeros planos auriculares se añadieron después del estado inicial y muestran la geometría final desde el interior y el exterior. `after-fur/` utiliza las mismas cámaras que `after-neutral/`; `after-sdf/` muestra el trazado SDF en GPU.

| Medida resuelta | Antes | Después |
| --- | ---: | ---: |
| Cráneo, ancho × alto × largo | 1.370 × 0.706 × 1.306 | 1.221 × 0.706 × 1.306 |
| Longitud longitudinal del hocico | 0.647 | 0.647 |
| Anchura de raíz del hocico | 0.867 | 0.684 |
| Anchura de punta del hocico | 0.418 | 0.330 |
| Pabellón, ancho × alto × espesor | 0.507 × 0.291 × 0.077 | 0.281 × 0.536 × 0.077 |
| Longitud cefálica efectiva | ≈1.708 | 1.673 |
| Anchura cigomática máxima | ≈1.343 | 1.157 |
| Relación anchura/longitud | ≈0.786 | 0.692 |
| Distancia entre ojos | ≈0.772 | 0.672 |
| Separación entre comisuras | ≈0.763 | 0.416 |

Los cinco primeros renglones proceden de la salida del demo antes y después. Las medidas restantes del estado inicial son reconstrucciones aproximadas de la receta resuelta anterior: todavía no existía `HeadAnatomy_Measure`. Las medidas finales proceden directamente de esa función. `cephalicIndex` sigue siendo un parámetro fenotípico del motor; la relación de esta tabla mide la geometría resuelta y no presupone un índice anatómico externo.

## Capturas

| Vista | Antes | Anatomía final | Pelaje final |
| --- | --- | --- | --- |
| Frontal | [PNG](before/frontal.png) | [PNG](after-neutral/frontal.png) | [PNG](after-fur/frontal.png) |
| Lateral izquierda | [PNG](before/lateral.png) | [PNG](after-neutral/lateral.png) | [PNG](after-fur/lateral.png) |
| Lateral derecha | — | [PNG](after-neutral/lateral-derecha.png) | [PNG](after-fur/lateral-derecha.png) |
| Tres cuartos | [PNG](before/tres-cuartos.png) | [PNG](after-neutral/tres-cuartos.png) | [PNG](after-fur/tres-cuartos.png) |
| Superior | [PNG](before/superior.png) | [PNG](after-neutral/superior.png) | [PNG](after-fur/superior.png) |
| Interior de oreja | — | [PNG](after-neutral/oreja-interior.png) | [PNG](after-fur/oreja-interior.png) |
| Exterior de oreja | — | [PNG](after-neutral/oreja-exterior.png) | [PNG](after-fur/oreja-exterior.png) |

El primer plano inicial [cabeza](before/primer-plano-cabeza.png) usa la cámara original del demo. Las vistas de `after-sdf/` sirven para inspeccionar el campo trazado en GPU, especialmente [concha](after-sdf/oreja-interior-sdf.png) y [dorso](after-sdf/oreja-exterior-sdf.png).

## Validación y coste

`make -B -j4 test demos` ejecuta todas las suites, incluida la conexión de una sola componente, la simetría, el marco auricular, el cuenco, los intervalos y la cobertura del pelaje. El demo SDF compara 1575 puntos de CPU y GPU alrededor de oreja, cuello y músculos; error absoluto máximo observado: 0.00000054. Las cuatro suelas permanecen sobre el suelo.

La compilación no emite advertencias del compilador. `make docs` todavía emite 1106 advertencias de Doxygen sobre símbolos del resto del proyecto sin documentar; los campos públicos añadidos para este rediseño sí quedaron documentados.

En esta máquina, el perfil de 60 fotogramas del primer plano auricular dio 5.891 ms GPU para malla desnuda, 45.090 ms GPU con pelaje y 146.845 ms GPU para trazado SDF. Son tres rutas distintas; no se conservó un perfil comparable del ejecutable inicial. El campo auricular añade la evaluación de anchura curva, cuenco y margen a cada muestra SDF, por lo que el incremento exacto respecto al estado inicial queda sin cuantificar.

# Controles de peinado y cobertura

Los grupos de pelo aceptan cuatro campos opcionales en las recetas v1 y en
`LongHairGroup`. Si se omiten, valen cero y conservan el peinado anterior.
También se interpolan durante el crecimiento y participan en la huella visual.

- `rootFullness` (0–1): acelera la apertura del tubo de hijos y aumenta el
  grosor de las cintas junto a la raíz. La separación sigue limitada por el
  espacio certificado contra el cuerpo; no se desplazan las raíces.
- `bend` (-2–2): curvatura progresiva hacia un lado del marco de la raíz.
- `wave` (0–2): ondulación de las guías en ese mismo marco. Se combina con
  `direction`, `stiffness` y `gravityInfluence`, que siguen controlando la caída.
- `transparency` (0–1): transparencia por fibra, multiplicada por el alfa de
  los colores de raíz y punta. Cero conserva el alfa original; uno es invisible.
  No equivale a la transparencia total del conjunto: varias fibras superpuestas
  acumulan cobertura. Con MSAA se usa alpha-to-coverage; sin MSAA se mezclan
  los fragmentos translúcidos sin ordenación por fibra, con posibles artefactos.
- `verticalBias` (-1–1): -1 orienta progresivamente las guías hacia abajo;
  0 conserva el peinado anterior; 1 genera guías rectas hacia arriba desde la
  raíz en reposo, antes de colisiones. Funciona independientemente de la
  rigidez y la gravedad; valores intermedios mezclan con el peinado existente.
  Es una dirección mundial de reposo, no una simulación dinámica: al animar se
  transporta con el esqueleto y el contacto puede desviar la curva, incluso en
  los extremos, para evitar penetraciones. Los hijos forman un tubo alrededor
  de las guías y no son todos líneas paralelas idénticas.

## Caída de crin y cola

Las siguientes capturas usan `verticalBias=-1` y `rootFullness=0.8`, con
raíces de la crin desplazadas hacia el lateral del cuello. Son recetas
optativas independientes, no modifican el catálogo del caballo.

| Peinado | Frontal | Lateral | Tres cuartos posterior | Receta y auditoría |
|---|---|---|---|---|
| Crin | [Ver](crin-caida-front.png) | [Ver](crin-caida-side.png) | [Ver](crin-caida-rear-three-quarter.png) | [JSON](caballo-curve-caida.creature.json), [log](crin-caida.log) |
| Cola | [Ver](cola-caida-front.png) | [Ver](cola-caida-side.png) | [Ver](cola-caida-rear-three-quarter.png) | [JSON](caballo-tail-caida.creature.json), [log](cola-caida.log) |
| Crin erguida (`verticalBias=1`) | [Ver](crin-erguida-front.png) | [Ver](crin-erguida-side.png) | [Ver](crin-erguida-rear-three-quarter.png) | [JSON](caballo-curve-erguida.creature.json), [log](crin-erguida.log) |

El banco registra 96 raíces visibles y paridad de guías CPU/GPU en ambos
casos; no detecta centros de hijos penetrantes en 115.200 muestras de reposo
por caso. El contacto es prioritario y produce pliegues visibles cerca de
algunas raíces, además de deformaciones: en la crin el peor segmento se alarga
un 110 % y la longitud total de una guía hasta un 12,2 %. La cola registra sus
propias cifras en el log. Esto no garantiza contacto exacto con el cuerpo
animado ni que el resultado visual esté terminado.

Las recetas antiguas se conservan en los JSON enlazados arriba. El generador
`--falling` produce ahora las variantes de varias bandas descritas a continuación:

```bash
python3 tools/generate_long_hair_examples.py --falling --output /tmp/opencode/long-hair-fall/recipes
```

## Bandas adicionales de raíces

El ejemplo optativo de crin distribuye ahora 192 guías en tres grupos: cresta
y ambos lados del cuello, en lugar de 96 guías en un solo lado. En la cola,
un segundo grupo con 64 guías cubre una sección más proximal del pedículo;
el grupo original de 96 conserva su distribución. Los grupos usan identificadores
independientes y no modifican los animales del catálogo.

| Peinado | Comparación anterior/nueva | Frontal | Lateral | Tres cuartos posterior | Datos |
|---|---|---|---|---|---|
| Crin | [Ver](crin-raices-comparacion-side.png) | [Ver](crin-raices-front.png) | [Ver](crin-raices-side.png) | [Ver](crin-raices-rear-three-quarter.png) | [JSON](caballo-curve-raices.creature.json), [log](crin-raices.log) |
| Cola | [Ver](cola-raices-comparacion-rear-three-quarter.png) | [Ver](cola-raices-front.png) | [Ver](cola-raices-side.png) | [Ver](cola-raices-rear-three-quarter.png) | [JSON](caballo-tail-raices.creature.json), [log](cola-raices.log) |

Las capturas usan la misma cámara que las anteriores. En reposo, la crin
conserva 192 raíces visibles de 192 y la cola 155 de 160; las cinco restantes
no superan la validación de inserción. No se detecta penetración en 230.400
y 186.000 centros de hijos muestreados respectivamente, y la paridad GPU de
guías pasa. Esto no prueba el contacto exacto con el cuerpo animado.

**Resultado parcial:** la distribución cubre la cresta y añade pelo cerca de
la cola, pero siguen viéndose huecos y pliegues en las raíces. La cola todavía
no presenta una transición completamente natural: la holgura conservadora
reduce la expansión de hijos junto a la piel y el contacto deforma guías.
El peor segmento se alarga un 118 % en crin y un 161 % en cola. El coste también
sube: 2,32/2,36 ms GPU y 6,75/23,79 ms CPU de pose en este banco aislado,
frente a 1,44/1,48 ms GPU y 3,69/15,45 ms CPU en las capturas anteriores.
No se ha resuelto aquí el problema general del contacto o la escalabilidad.

## Salida proximal y crin hacia un lado

La receta optativa distribuye las tres bandas de crin en el mismo lado de la
cresta, con caída hacia ese lado. El certificado de holgura ya no obliga a
cerrar completamente la expansión durante todo el primer tramo: se usa la
distancia en el primer control y una cota Bézier de velocidad para permitir
apertura allí donde el campo deja espacio. El radio vuelve gradualmente al
certificado anterior antes del segundo tramo. Este razonamiento es conservador
para campos de distancia 1-Lipschitz; los proxies animados siguen siendo una
aproximación. No cambia el anclaje ni añade guías o hijos.

| Peinado | Comparación | Frontal | Lateral | Tres cuartos posterior | Datos |
|---|---|---|---|---|---|
| Crin | [Ver](crin-salida-comparacion-front.png) | [Ver](crin-salida-front.png) | [Ver](crin-salida-side.png) | [Ver](crin-salida-rear-three-quarter.png) | [JSON](caballo-curve-salida.creature.json), [log](crin-salida.log) |
| Cola | [Ver](cola-salida-comparacion-rear-three-quarter.png) | [Ver](cola-salida-front.png) | [Ver](cola-salida-side.png) | [Ver](cola-salida-rear-three-quarter.png) | [JSON](caballo-tail-salida.creature.json), [log](cola-salida.log) |

En las capturas adultas sin MSAA se muestrearon 230.400/186.000 centros de
hijos en reposo sin penetración detectada y pasó la paridad de guías CPU/GPU.
El test contra un plano comprueba que la apertura temprana no supera la
distancia disponible. Los contactos continúan deformando algunas guías y
quedan tallos y pliegues visibles en algunas raíces: la mejora de apertura
no garantiza que todo mechón quede pegado a la piel. El coste GPU registrado
es 2,59/2,22 ms y la actualización CPU de pose 7,43/24,08 ms; son mediciones
de estos primeros planos, no un presupuesto de ecosistema.

## Comprobación puntual

[Comparación frontal](caballo-frontal.png): referencia de volumen, ejemplo
con `rootFullness=0.8`, `bend=0.3`, `wave=0.25`, `transparency=0` y el mismo
ejemplo con `transparency=0.98`. Misma cámara, caballo adulto, calidad 3,
900 × 900, sin MSAA. Es una comprobación de un caso, no una validación visual
exhaustiva de todas las especies, poses o valores de transparencia.

Vistas adicionales del caballo adulto, con los controles de peinado activados:

| Peinado | Frontal | Lateral | Tres cuartos posterior |
|---|---|---|---|
| Crin | [Ver](crin-front.png) | [Ver](crin-side.png) | [Ver](crin-rear-three-quarter.png) |
| Cola | [Ver](cola-front.png) | [Ver](cola-side.png) | [Ver](cola-rear-three-quarter.png) |

La cola emplea `rootFullness=0.8`, `bend=0.3`, `wave=0.25` y
`transparency=0`, además de sus parámetros propios de dirección y longitud.
En [cola.log](cola.log) constan 96 raíces visibles, paridad CPU/GPU y ninguna
penetración de los 115.200 centros de hijos muestreados en reposo. La máxima
deformación relativa de longitud total es 14,5 % y la local de segmento 131 %.
La muestra utiliza la misma cámara que la referencia anterior de la cola.

En [capture.log](capture.log), las 96 raíces son visibles, la paridad de
guías CPU/GPU tiene error máximo 0,00000144 y no se detecta penetración en
115.200 centros de hijos muestreados contra el SDF corporal de reposo. El
residuo de los proxies posados también es cero. La longitud total máxima de
una guía difiere un 15,4 % y el peor segmento local un 138 %: el contacto tiene
prioridad sobre la inextensibilidad. Estos valores y el déficit de envolvente
no garantizan contacto perfecto con la piel deformada. La medición de pelo
es 1,36 ms GPU en Radeon Renoir; no es una medida de escalabilidad.
[transparent.log](transparent.log) registra la segunda captura.

Para generar las recetas optativas sin sobrescribir la evidencia anterior:

```bash
make build/capture_long_hair build/creature_editor_bridge
python3 tools/generate_long_hair_examples.py --styled --output /tmp/opencode/long-hair-styled/recipes
mkdir -p /tmp/opencode/long-hair-styled/capture
LONG_HAIR_CAMERA="$PWD/docs/validation/long-hair-volume/before/caballo-curve-stage-3/camera.txt" \
  build/capture_long_hair /tmp/opencode/long-hair-styled/recipes/caballo-curve.creature.json /tmp/opencode/long-hair-styled/capture 3
```

Para reproducir la tercera imagen, cambiar `transparency` a `0.98` en los
cuatro grupos de etapa de una copia de la receta y capturar con la misma cámara.

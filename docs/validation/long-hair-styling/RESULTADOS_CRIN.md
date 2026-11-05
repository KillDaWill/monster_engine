# Crin: folículos distribuidos y caída unilateral

## Evidencia visual

Capturas reales de Monster Engine/OpenGL, 900 × 900, calidad 3, sin MSAA.
La cámara adulta es la de la referencia original; no se retocan pelo, colores
ni iluminación. La comparación incluye cambios de distribución y peinado,
no es una comparación a igual número de guías/fibras.

| Vista | Evidencia |
|---|---|
| Lateral, antes y después | [comparacion-crin-side.png](comparacion-crin-side.png) |
| Raíz ampliada, antes y después | [comparacion-raiz.png](comparacion-raiz.png) |
| Frontal: caída al mismo lado | [comparacion-crin-front.png](comparacion-crin-front.png) |
| Tres cuartos posterior | [comparacion-crin-rear-three-quarter.png](comparacion-crin-rear-three-quarter.png) |
| Tres poses | [crin-poses.gif](crin-poses.gif) |
| Cuerpo completo | [whole-q3.png](resultado/caballo-curve-stage-3/whole-q3.png) |

La raíz ya no está formada por hijos coincidentes que sólo se separan lejos
de la piel. Se observa una cobertura continua de fibras finas desde la inserción,
con caída hacia un único lateral. Quedan pequeñas irregularidades de peinado
y algunos cruces entre fibras: no se ha implementado colisión pelo–pelo.

## Cambio del módulo

- `LongHair.c`: cada hijo de curva con `rootFullness > 0` elige entre cuatro
  guías próximas del mismo grupo, con normales y direcciones compatibles.
  Su raíz interpolada se proyecta sobre el SDF corporal. Se auditan 72 muestras
  de la fibra; si no hay holgura, se reduce el peso hasta cinco intentos.
  El comportamiento tubular queda como respaldo para casos no válidos.
- `long_hair.vert`: interpola las curvas transportadas, conserva la corrección
  normal de inserción y elimina el engrosamiento proximal de los hijos con
  folículo propio. No se añade una malla ni un tubo opaco para ocultar huecos.
- Se transportan 80 `vec4` por guía: 16 filas originales y 64 descriptores de
  folículo. No se transportan vértices de cada fibra desde CPU.
- La receta usa tres grupos de 96 guías × 32 hijos: **9.216 fibras**. La banda
  angular va de 0,075 a 0,525 radianes, con dirección lateral +X y caída progresiva.
  Se conserva el formato JSON existente y se ofrece `--rooted-mane` en el generador.

La receta lista para cargar es
[caballo-crin-foliculos.creature.json](caballo-crin-foliculos.creature.json).

## Verificación visual y geométrica

El validador de las cuatro etapas terminó con código 0:
[validacion.log](validacion.log), [capturas y huella del binario](resultado/captures.json).

| Comprobación | Resultado |
|---|---|
| Guías visibles por etapa | 288/288 |
| Hijos con folículo propio por etapa | 9.216/9.216; ninguno usa el respaldo tubular |
| Guías con punta desplazada hacia +X | 288/288 en las cuatro etapas |
| Muestras GPU de fibras por etapa | 230.400; penetración máxima detectada 0 |
| Error máximo de proyección adulta respecto al margen cutáneo | 0,00000344 unidades |
| Paridad de curvas CPU/GPU adulta | pasa; error máximo 0,00000145 |
| Paridad de inserciones CPU/GPU | comprobada en el primer vértice durante la auditoría GPU |
| Identidades anatómicas entre etapas | [estables](resultado/root-stability.json) |
| Cambio máximo de longitud total de guía adulta | 0,390 % |
| Deformación máxima de un segmento adulto | 3,511 % |

Las cuatro etapas contienen lateral/frontal/tres cuartos, calidades 0–4 y
tres poses en `resultado/caballo-curve-stage-{0,1,2,3}/`.
El log de [adulto](resultado/caballo-curve-stage-3/capture.log) registra además
la animación sin reconstrucción de malla y 21 pasos de crecimiento.

El test `FollicleTests` comprueba proyección sobre un cuello curvo, distribución
independiente, determinismo y ausencia de penetraciones entre las muestras
de construcción. Resultado de la suite específica: [pruebas-pelo.log](pruebas-pelo.log).
El registro de las comprobaciones generales está en [pruebas.log](pruebas.log).
**Pasan `make test`, `make editor-test` (10 pruebas) y `git diff --check`.**
Un primer intento concurrente con las capturas falló en la espera de una malla
LOD del ecosistema (`AppendageMeshContinuity`); la repetición aislada pasó
sin cambiar esa prueba ni el código del ecosistema.

Con `--rooted-mane`, el validador exige que todos los hijos tengan folículo
propio y que todas las guías caigan hacia +X; una regresión en cualquiera de
estas condiciones hace fallar la ejecución.

## Coste y límites pendientes

Banco adulto Radeon Renoir: **2,85 ms GPU** de pelo, **9,23 ms CPU** por pose,
476.292 bytes de runtime y 368.640 bytes de buffer GPU. El tiempo de cuadro
del banco excluye el bucle separado de actualización de pose.

La auditoría de inserciones eleva la construcción a **1,52 s**, y la actualización
de pelo durante los 21 pasos de crecimiento promedia **1,47 s/paso**. Esto
**no cumple un presupuesto de morph en tiempo real**; falta cachear/acelerar
esa construcción. [Mediciones completas](resultado/performance.json).

El muestreo demuestra los casos capturados, no una garantía continua para
cualquier cuerpo. La colisión de guías posadas sigue usando proxies; no se
ha demostrado contacto exacto de todos los hijos contra la piel deformada.
La selección de vecinos se recalcula al reconstruir y no garantiza todavía
la continuidad de cada folículo a través de cualquier morph arbitrario.

## Reproducción

Desde la raíz del repositorio, con SDL2/OpenGL y Pillow disponibles:

```bash
make build/capture_long_hair build/creature_editor_bridge
python3 tools/validate_long_hair.py --rooted-mane --all-stages \
  --case caballo-curve --output docs/validation/long-hair-styling/resultado
python3 docs/validation/long-hair-styling/componer_pruebas.py
make test
make editor-test
```

Se conserva `crin-side.png` como referencia original. Se retiró el directorio
intermedio `foliculos/` para no confundir ensayos descartados con el resultado validado.

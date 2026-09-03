# Informe de rediseño anatómico de cabeza y boca

## 1. Objetivo y alcance

La boca dejó de ser un conjunto de adornos superpuestos. La nueva raíz es `Head`: conserva un `HeadPhenotype` semántico, resuelve `HeadLandmarks` y compila una `HeadSurfaceRecipe`. `Mouth` queda como `oralSystem` de la anatomía resuelta y sólo concentra hendidura, comisuras, mandíbula, pivote, bisagra y garganta.
El alcance corrige tres causas raíz: la mandíbula duplicada en el SDF corporal y en la visual, el cutter oral que no alcanzaba la superficie facial real y la huella que confundía pose con forma.

## 2. Modelo geométrico

`MonsterSDF_Build` consume la receta resuelta y compone cráneo, rostro ahusado, mejillas, cejas, rebordes orbitales, almohadilla nasal y orejas según el arquetipo. Después sustrae la cavidad oral, las dos órbitas y las dos narinas del campo corporal completo. Esta última condición evita que el elipsoide anfitrión vuelva a rellenar los cutters. La mandíbula y el puente gular se evalúan en campos locales separados.

## 3. Materiales

`SDFSample.material` se conserva en `MeshVertex`. Las superficies exteriores son `SDF_MATERIAL_SKIN`; las paredes producidas por la sustracción son `SDF_MATERIAL_MOUTH`. La mandíbula local conserva ambos materiales; el puente gular es piel y no se etiqueta falsamente como tejido oral.
`lipColor` permanece reservado y no se incluye en fingerprints porque no pinta ninguna superficie activa.

## 4. Articulación

El ángulo es `clamp(openFactor) * maxJawAngle`. La mandíbula posee una malla base inmutable y cada actualización copia posiciones y normales desde esa base mediante una transformación inversa alrededor de `jawPivot`; no se transforma acumulativamente.

## 5. Runtime síncrono y asíncrono

Los cambios estáticos modifican la huella geométrica y remallan. Un cambio exclusivo de `openFactor` sólo actualiza la mandíbula visible. El worker asíncrono genera cuerpo, ojos y los componentes cerrados mandíbula/bisagra; el hilo principal aplica la articulación sin encolar un remallado completo.
La ruta asíncrona usa el índice explícito del snapshot para cada boca; antes todas las bocas podían seleccionar accidentalmente el índice cero.

## 6. Contrato de render

La boca expone dos componentes: mandíbula y bisagra. La cavidad pertenece al cuerpo SDF, por lo que no existe túnel independiente ni contrato de cinco overlays. El renderizador continúa siendo una VTable agnóstica.

## 7. Depuración

`MonsterHeadDebugMode` permite inspeccionar FULL, CRANIUM, SNOUT, UPPER_HEAD, JAW, BRIDGES, CAVITY y SLIT mediante `MonsterSDF_EvaluateDebug`.

## 8. Validación topológica

`Mesh_Validate` mantiene las comprobaciones de índices, finitud y área, y además contabiliza aristas frontera, aristas no-manifold, triángulos duplicados y vértices aislados. Las banderas `manifold` y `watertight` distinguen una malla abierta válida de una superficie cerrada.

## 9. Envejecimiento y copias

Cuando ambos extremos poseen `Head`, `MonsterAger` interpola `HeadPhenotype` y vuelve a resolver landmarks y receta. La interpolación de arrays continúa como puente para criaturas heredadas. `Monster_Clone` copia el modelo semántico además de conservar la semántica existente de arrays y punteros de traits.

## 10. Resolución y presupuesto

El campo local de mandíbula solicita `voxelSize=0.04` y la costura blanda `0.03`, separados del presupuesto corporal. El tier asíncrono interactivo usa `0.12` con 250 000 celdas y el asentado `0.08` con 500 000; los límites pueden coarsenizar la resolución efectiva en criaturas grandes.

## 11. Pruebas

Las pruebas cubren conexión exterior, límite posterior, material oral, pivote, estados abierto/cerrado, articulación sin remallado corporal, componentes bilaterales de mandíbula/bisagra, finitud y auditoría topológica, además del contrato asíncrono. La suite de cabeza valida los tres presets, cutters orbitales y nasales reales, interpolación semántica, la primitiva elíptica ahusada, extracción de malla para cada arquetipo y 540 anatomías aleatorias legales en tres escalas.

## 12. Demos y documentación

El visor y las demos siguen usando `openFactor`; la demo dedicada muestra la articulación. `README.md` documenta el nuevo flujo y las teclas.

## 13. Limitaciones y siguientes pasos

La visualización de material todavía usa el pipeline de color fijo de OpenGL; el identificador queda disponible para un shader futuro. La mandíbula no participa en el campo corporal: se genera desde su campo local y se renderiza separadamente. La costura posterior se genera desde un único campo SDF conectado y se deforma en CPU entre anclas craneales fijas y mandibulares móviles.
El array `BodyPart[]` todavía es una cadena implícita y los arrays `mouths`/`eyes` siguen presentes como puente para el renderer y demos existentes. El siguiente paso es introducir nodos axiales con `AnatomyId`, padre explícito y transform de reposo; entonces `HeadLandmarks.neckAttachment` podrá apuntar al nodo NECK sin depender del índice cero. Después pueden migrarse los conectores SDF de `i -> i + 1` a aristas explícitas.

## 14. Parámetros y extensión de arquetipos

`HeadPhenotype` expone proporciones de cráneo, hocico, ojos, mandíbula, nariz, orejas y pico en el rango normalizado `[0, 1]`. `HeadPhenotype_Normalize` constituye la frontera pública de validación. El resolver deriva las coordenadas dependientes y garantiza simetría bilateral, bisagras posteriores a las comisuras, rostro anterior al cráneo, órbitas dentro de la envolvente craneal, narinas dentro del tramo facial y anclaje cervical posterior/inferior.

Para añadir otro arquetipo:

1. Añadir el valor a `HeadArchetype` y un preset semántico; no añadir coordenadas al preset.
2. Ajustar sólo las reglas de proporción necesarias dentro de `HeadAnatomy_Resolve` y expresarlas en `HeadSurfaceRecipe` genérica.
3. Reutilizar primitivas SDF generales; una primitiva nueva necesita pruebas matemáticas propias.
4. Añadir el preset al barrido aleatorio, a la validación de cutters y a la extracción de malla.
5. Mantener la mandíbula o pico inferior en `HeadAnatomy.oralSystem` y la pose en `openFactor`, sin remallar el cuerpo por fotograma.

La validación visual se realizó con capturas temporales del demo de crecimiento y de la demo mandibular en estados abierto, intermedio y casi cerrado; las capturas no se almacenan como artefactos del repositorio.

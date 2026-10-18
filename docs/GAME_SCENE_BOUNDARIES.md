# Contención y navegación de la escena

La plataforma de `demo_game_scene` mide ahora 56×56 m. Las paredes invisibles
están en X/Z = ±28; las guías de depuración coinciden con ellas. `--stage-half`
permite ensayar otras dimensiones sin modificar la geometría de las criaturas.

## Causa de la regresión y reparación

El contacto recorría todas las articulaciones, probaba ambos sentidos de flexión
y recalculaba la envolvente completa por cada prueba. También repetía pose e IK
en cada subpaso de 1/60 s: una caída de FPS multiplicaba de nuevo ese coste.
La navegación durante una mutación podía consultar una pose en reposo recién
reconfigurada, aunque `bodyPosition` ya había avanzado, desplazando la huella.

Ahora se comprueba la contención en O(J), se sale directamente si la pose está
libre y se limita la flexión a ocho articulaciones afectadas por contacto.
La presión se propaga por la jerarquía en un recorrido y la IK adicional sólo
se resuelve si cambió la pose. Los apoyos conservan los subpasos internos de
`Locomotion`; la pose y la contención se publican una vez por fotograma.
Los transformados de reposo y las cotas de segmentos se precalculan.

`MonsterAnimation_Configure` conserva rotaciones por identidad y el desplazamiento
locomotor de la raíz. La pose publicada está actualizada antes de que la
navegación la consulte, sin avanzar el tiempo ni duplicar la IK.

La navegación anticipa la huella del giro y mantiene un objetivo interior durante
la maniobra. Si el avance real se bloquea, retrocede mediante la marcha procedural.
La flexión de la cola depende de su longitud y de la curvatura de la trayectoria,
con un arco finito, independiente del número de estaciones y compatible con
marcha atrás. El margen de pelaje sólo se aplica a tejidos que tienen pelaje.

La envolvente de seguridad sigue siendo conservadora. Un contacto fuerte o
crecimiento cerca de una esquina puede exigir trasladar la raíz para contener
la superficie completa; no se promete una simulación de cuerpo blando exacta.
La navegación anticipa ese espacio y la flexión responde al contacto anatómico.
No hay colisiones entre criaturas en esta reparación.

## Verificación

```sh
make -B -j4 test demos
demos/demo_game_scene --headless --validate-navigation --frames=300 --target=2 --seed=42
demos/demo_game_scene --headless --validate-navigation --frames=1200 --cycles=3 --seed=173 --fixed-dt=.05
```

Las suites completas pasan. Se comprueban las diez variantes en 32 orientaciones,
esquinas, contención de vértices CPU y muestras de skinning GPU, flexión sin
sustituirla por traslación en un caso de contacto local, presupuesto de pruebas
acotado, pose locomotora válida al reconfigurar y arco de cola independiente de
la segmentación. La ejecución larga completó 60 s simulados y tres mutaciones:
Stone Armored, Sand Burrower y Emerald Climber. El avance solicitado realmente
recorrido fue 77,10 m para el perro y 42,38 m para el lagarto; el bloqueo máximo
fue 0,30 s y el escape máximo 0,00000191 m, dentro de la tolerancia de 0,0001 m.

Los perfiles comparan la versión anterior con la entregada sobre AMD Radeon
Renoir/Mesa 25.2.8, sin otros procesos de pruebas durante la medición. Se mantiene
voxel 0,075, recetas completas, accesorios, materiales y pelaje. Tras Blue Runner
se publican los mismos 91.128 índices. La plataforma, encuadre y trayectorias
cambian; las cifras de fotograma representan el resultado conjunto, no una
comparación de GPU con idéntica cantidad de píxeles.

| Prueba: mediana por fotograma | Antes | Después |
|---|---:|---:|
| Blue Runner durante mutación | 41,82 ms | 26,66 ms |
| Blue Runner estable | 29,39 ms | 13,75 ms |
| Escena con tiempo real, sin mutación | 27,43 ms | 9,26 ms |

La optimización del cálculo de contacto también se ensayó antes de ampliar la
plataforma: bajó de aproximadamente 19,31 a 5,74 ms en el escenario estable
original de 26×26 m. En la escena entregada, la mediana estable del contacto es
aproximadamente 0,07 ms. Estas medidas no certifican 60 FPS para todas las variantes
ni eliminan los picos heredados de extracción GPU durante mutaciones complejas.

[Perfiles, registros y capturas](validation/game-scene/boundary-performance/).
Evidencia final: `build-tests-pose.log`, `after-pose.csv`, `after-realtime.csv`,
`verified-long.log` y `verified-long.png`. Los archivos `weighted*` documentan
experimentos de diagnóstico y no corresponden al código entregado.

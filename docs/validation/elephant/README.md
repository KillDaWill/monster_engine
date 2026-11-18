# Elefante africano de sabana

`Elefante` (ID 1008) se carga desde el catálogo normal. La demo universal
lo ofrece en el selector bajo la tecla **9** y admite `elephant` y
`african-elephant` en `--from` y `--to`.

## Referencias anatómicas

- Las fotografías del macho adulto guían la depresión dorsal, la inserción
  cervical corta, la anchura de la cara y el arco de los incisivos. El
  perfil y la vista oblicua muestran cuánto cubre el pabellón la escápula.
  [Perfil del macho](https://commons.wikimedia.org/wiki/File:African_Elephant_(Loxodonta_africana)_bull_...._(50122069972).jpg),
  [macho de frente a tres cuartos](https://commons.wikimedia.org/wiki/File:Day_35_Savanna_Elephant_(Loxodonta_africana)_aroused_bull_..._(53283719827).jpg).
- El perfil posterior y las fotografías de crías guían la caída del maslo,
  la proporción relativa de orejas y cráneo, y el acortamiento de la trompa.
  [Vista posterior de Kathy Karn](https://kathykarn.com/blog/2020/11/11/elephant-ears),
  [crías del San Diego Zoo](https://animals.sandiegozoo.org/animals/elephant).
- Los cojines plantares y las cuatro extremidades sustentan el volumen.
  El ejemplar de la receta usa cinco uñas anteriores y cuatro posteriores.
  [Biología en SANParks](https://www.sanparks.org/conservation/parks/kruger/letaba-elephant-hall/about-elephants/african-elephant-biology),
  [proporciones y huellas del Zoológico de Taipéi](https://www.zoo.gov.tw/elephant_exhibition/elephants_footprint.htm).
- Se tomaron como referencia el tabique distal y los dedos dorsal y ventral
  para el extremo de la trompa. El relieve transversal se combina con
  fisuras cortas e irregulares. [Morfología de la punta y prensión](https://edoc.hu-berlin.de/items/9516d16b-05ff-433e-b495-d0ec2eca5501),
  [pliegues y extensión asimétrica de la piel](https://doi.org/10.1073/pnas.2122563119).
  La reconstrucción del PMID 37757829 describe el mecanismo hidroestático
  en una cría asiática. Para la forma de la punta se usaron los ejemplares
  de sabana del estudio comparativo.
  [Reconstrucción de fascículos](https://pubmed.ncbi.nlm.nih.gov/37757829/).

## Implementación e iteración

El cuerpo reutiliza las estaciones axiales erectas. La cabeza publica una
inserción nasal y dos inserciones maxilares. La trompa usa veinte estaciones
de radio elíptico y una rama corta por dedo terminal. El extremo resta dos
cavidades locales, con cota conservadora y material de mucosa. Los gestos
modifican la pose de la cadena con longitudes óseas constantes. La malla y
las coordenadas de material siguen la pose mediante el deformador existente.

Se revisaron las siluetas anterior, lateral, posterior y oblicua en material
neutro. Entre la primera y la segunda revisión se cerró el cuenco de las
orejas y se redistribuyó su cartílago hacia el hombro. Se redujo la mezcla
repetida entre estaciones de la trompa y se integró el apoyo distal con la
caña. Se retocaron el contorno cóncavo del dorso, el arco de los incisivos y
la caída de la cola. La última pasada añadió irregularidad al relieve de
piel y contraste en las paredes nasales. El pelo terminal reutiliza guías
de pelo largo ancladas a las últimas estaciones de la cola.

Las metamorfosis revelaron que interpolar ejes de oreja orientados en
sentidos opuestos podía producir un marco casi singular. Ahora el marco
rota de forma ortonormal desde la inserción y vuelve a calcular su AABB.
El cuenco se cierra y el pabellón se pliega hacia su raíz al rotar hacia
la posición pendular. El plegado conserva la inserción y atenúa los
recortes visibles durante el intercambio de arquetipo. La trompa colapsa hacia la cabeza cuando el otro extremo carece
del módulo. Los incisivos mantienen su identidad y la dentina en el
grafo unión.

La revisión con un worker retrasado mostró un cráneo anterior sobre el
campo nuevo. La demo pasaba cajas a una API de recorte obsoleta. Ahora la
malla y el raymarch comparten las interfaces de cesión. El dominio
cefálico cubre las dos envolventes de extremo y la malla ya publicada,
incluidas orejas y mandíbula. La demo dibuja el campo corriente completo
mientras el worker prepara la primera malla con material de superficie.

## Capturas del motor

- [Adulto en cuatro vistas, cabeza y paso](adulto.jpg)
- [Silueta neutra en cuatro vistas](silueta.jpg)
- [Las cuatro etapas y edad intermedia](crecimiento.jpg)
- [Reposo, rizo, extensión y alimentación](trompa.jpg)
- [Metamorfosis en ambos sentidos](metamorfosis.jpg)
- [Comparación de render a la misma edad](renderizado.jpg)

Las láminas recortan y agrupan capturas del motor. Los PPM completos y
los registros quedan en `build/elephant/delivery/` y
`build/elephant-morph/delivery/`.

## Verificación y coste

- La prueba C recorre cuatro etapas y tres edades intermedias. Comprueba
  la ida y vuelta JSON, los dos lúmenes y su tabique, las cuatro suelas,
  las 18 placas ungueales y el crecimiento de los incisivos. Comprueba
  además la ortogonalidad de las orejas y la presencia de la trompa en
  ambas direcciones de metamorfosis.
- En suelo plano, a 0,55 unidades por segundo, la marcha adulta mantuvo
  fijos los objetivos de apoyo. En 395 muestras de fase de contacto, el
  mayor error del extremo IK fue de 0,00195 unidades.
- CPU, compute y fragmento coincidieron en 7.537 muestras por etapa.
  El máximo error de distancia en las cuatro etapas fue `9,54e-7`
  unidades. La validación de intervalos del pabellón dio un error máximo
  de `7,16e-7`.
- Se capturaron 11 fotogramas de ida y vuelta por ciclo en los siete pares
  solicitados. A 0,50 en Caballo → Elefante, la intersección sobre unión
  de las máscaras de imagen fue 99,69 % entre SDF exacto y raymarch.
  Con 180 ms de retraso del worker, fue 99,43 % en la ida y 99,68 % en
  el retorno frente a la misma captura exacta. Antes del cambio de cesión,
  el solapamiento en la ida era 94,32 %. El umbral de máscara fue una
  diferencia de luminancia de 14 niveles respecto al fondo.
- En Cerdo → Elefante a 0,50 se comparó la malla del campo CPU con raymarch
  y las extracciones GPU densa y dispersa, con la misma cámara. Los 64
  puntos del diagnóstico compute coincidieron con CPU a cinco decimales.
  La captura del modo `exact-sdf` de fragmento para ese par quedó vacía
  con 512 y 1.024 pasos en la RTX 3050. Se detuvo una ejecución con paso
  máximo 0,02 después de cinco minutos sin fotograma. El panel CPU se
  extrajo del mismo snapshot de la metamorfosis, con vóxel solicitado
  de 0,024 y 429.859 vértices.
- El muestreo CPU de 700.000 puntos por pasada dio una mediana de 1.622 ms
  para Cerdo en `18c25de` y 1.654 ms en esta rama, con 66 conectores y la
  misma suma de distancias en las siete pasadas. Elefante adulto dio
  1.998 ms con 93 conectores sobre su propia caja. Estas cifras miden
  muestreo del campo en CPU. La mediana de Cerdo subió aproximadamente
  un 2 %. No se midió el tiempo por fotograma.
- La captura adulta produjo 402.395 vértices y 3.473.991 hojas adaptativas,
  con vóxel efectivo de 0,020 frente al 0,018 solicitado. Cerdo mantuvo
  256.586 vértices y 1.382.168 hojas en la comparación con el binario
  anterior. La animación de la trompa deforma la malla de reposo.

## Alcance visual

La silueta y el material presentan rasgos de un macho de sabana. Los
bordes de las orejas aún se ven rígidos en algunos ángulos. La extracción
de malla puede facetar los dedos terminales y las uñas a menor resolución.
El gesto de alimentación aproxima el extremo a la boca mediante una curva
predefinida. Las extracciones GPU a 0,08 unidades por vóxel muestran
muescas en pabellones delgados y facetas cerca de los incisivos en la
transición Cerdo → Elefante. La marcha se midió en suelo plano, y la precisión de contacto
en terreno irregular y la colisión de la trompa durante gestos arbitrarios
quedan por verificar. Las orejas conservan su pose de reposo durante la
marcha.

## Reproducción

```bash
make test
make editor-test
make demos/demo_morph_species build/capture_creature build/validate_recipe_gpu
./demos/demo_morph_species --list
mkdir -p build/elephant/adult build/elephant-morph
./build/capture_creature recipes/elefante.creature.json build/elephant/adult 1 surface
./build/validate_recipe_gpu recipes/elefante.creature.json 3
./demos/demo_morph_species --from=Caballo --to=Elefante --validate-cycle=build/elephant-morph/caballo
./demos/demo_morph_species --from=Cerdo --to=Elefante --validate-cycle=build/elephant-morph/cerdo
./demos/demo_morph_species --from=Elefante --to=Lagarto --validate-cycle=build/elephant-morph/lagarto
```

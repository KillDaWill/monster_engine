# Rediseño anatómico de cabeza y boca

La extensión corporal y el muestreo conformante se documentan en
[Validación de lagarto y crecimiento](lizard-growth-validation.md).

## 1. Autoridad semántica

La ruta productiva conserva `HeadPhenotype -> HeadAnatomy -> HeadSurfaceRecipe`.
Los presets sólo fijan proporciones semánticas; `HeadAnatomy_Resolve` deriva
landmarks, secciones, cutters y anclajes concretos. El envejecimiento interpola
`HeadPhenotype` y vuelve a resolver la anatomía, de modo que no interpola una
malla ni coordenadas de render arbitrarias.

Los controles añadidos son `eyeDorsality`, `eyeExposure`, `browProminence` y
`snoutBluntness`. Permiten variar una anatomía futura de tipo escíncido, agámido,
lacértido, gecko o varano sin codificar otra cabeza en el renderer.

## 2. Distribución de masa

El cráneo del lagarto es un barrido dorsoventralmente aplanado. Las masas
temporal, maxilar y malar son bilaterales, más pequeñas y con uniones escaladas
por la dimensión local. La región orbitotemporal conserva la anchura máxima y
el rostro crea una constricción preorbital antes de estrecharse hacia el morro.

`headBodySmoothness` es independiente de `mouthSmoothness`: se deriva de los
radios locales del cráneo y del cuello. Por tanto, el valor oral global no puede
volver a inflar o borrar la transición occipital.

## 3. Rostro multisección

La cabeza anatómica ya no usa `SDF_RoundedTaperedWedge`. La receta contiene
`faceRoot`, `faceMid` y `faceTip`, cada uno con centro Y y radios independientes.
Para el lagarto, `SDF_EllipticalSweepZ` incorpora estas secciones al barrido
craneal continuo. Otros arquetipos conservan `SDF_ThreeSectionEllipticalLoftApprox`.
El taper es no lineal, `rostrumDorsalSlope`
desplaza realmente el perfil vertical y `snoutBluntness` conserva un extremo
premaxilar comprimido pero redondeado.

La primitiva es una SDF aproximada: se optimiza la continuidad de la superficie
cero y del gradiente para Marching Cubes, no una distancia euclídea exacta. Éste
es el mismo contrato que las cápsulas ahusadas aproximadas existentes.

## 4. Órbita, párpado y globo

Para el preset de lagarto, `eyeLaterality` se mapea al intervalo aproximado
`0.72..0.80` del semiancho craneal. `eyeDorsality` controla la altura. El centro
del globo sólo avanza una fracción pequeña y explícita (`eyeExposure`) sobre la
normal orbital; la validación limita esa exposición al 25 % de la profundidad
del globo y exige que órbita y ojo sigan dentro de la envolvente craneal.

Los rebordes y cejas son bilaterales. Cada volumen acompaña su órbita y penetra
la masa craneal; no existe un elipsoide central entre ambos ojos. El cutter se
adelanta hacia la normal exterior para crear una cavidad abierta, evitando una
lámina cutánea delante del ojo. El globo, iris y pupila siguen siendo mallas
analíticas separadas y orientadas por la normal, lo que preserva el detalle sin
depender de la rejilla SDF.

## 5. Superficie craneocervical conformante

El lagarto usa una única malla para cuerpo y cabeza. Su cráneo y rostro comparten
un barrido de seis estaciones; la transición al cuello se une en el campo antes
de extraer Marching Cubes. Los getters de cuerpo/cabeza separados permanecen como
herramientas de diagnóstico y ruta de compatibilidad para otras criaturas.

La mandíbula y su tejido articulado conservan sus mallas propias. Un suelo
volumétrico une las ramas mandibulares. La resolución semántica sigue siendo la
autoridad de ojos, órbitas, bisagra, rostro y cutters.

## 6. Detalle por escala de rasgo

`MonsterSDF_GetDetailRegions` produce AABB y objetivos locales a partir de narinas,
órbitas y tímpanos. `SDFMesher_GenerateMeshDetailed` construye ejes rectilíneos con
espaciado variable y conectividad compartida. Los objetivos son 3,5 muestras por
diámetro en interacción y 6 en reposo; las estadísticas indican cualquier
incumplimiento por presupuesto. La cola mantiene un espaciado mayor.

La caché de vértices nodales evita agujeros al coincidir una isosuperficie con un
nodo de rejilla. El criterio de área relativa conserva triángulos pequeños
válidos. Los gradientes cacheados usan el espaciado no uniforme real.

## 7. Visualización y verificación

El demo permite edades fijas con 0..4, vistas cefálicas con H/F1..F4 y zoom con la
rueda. Las capturas mantienen cámaras idénticas a todas las edades. El worker
publica todos los componentes junto con sus métricas; evita solicitudes duplicadas
y sólo aplica pose en vivo sobre una geometría coincidente.

Las pruebas incluyen cavidades subvoxel, estanqueidad de los casos medidos,
propiedad semántica durante crecimiento, continuidad, simetría, poses y
coalescencia. El informe enlazado contiene las imágenes reales antes/después,
los presupuestos, benchmarks y limitaciones. No se afirma que Marching Cubes
canónico resuelva toda ambigüedad topológica de cualquier CSG arbitraria.

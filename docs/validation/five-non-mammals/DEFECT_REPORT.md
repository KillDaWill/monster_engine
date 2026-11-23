# Informe de Validación Visual e Iteración Anatómica (Especies 1016–1020)

Evidencia visual generada con `build/capture_creature` (OpenGL real sobre NVIDIA GeForce RTX 3050 Laptop GPU) en tres pases iterativos de inspección y corrección sobre los módulos propietarios del motor.

## Hojas de evidencia final

- [00_neutral_geometry_overview.png](00_neutral_geometry_overview.png) — Silueta neutra de las 5 especies (geometría pura sin coloración).
- [01_halcon_peregrino.png](01_halcon_peregrino.png) — *Falco peregrinus* (ID 1016): 6 vistas ortogonales/3/4, cabeza, pata prensil, y aleteo animado en 2 fases.
- [02_mantarraya.png](02_mantarraya.png) — *Mobula birostris* (ID 1017): 6 vistas (incluidas 5 pares de branquias ventrales), lóbulos cefálicos y natación ondulatoria en 2 fases.
- [03_pulpo_gigante.png](03_pulpo_gigante.png) — *Enteroctopus dofleini* (ID 1018): 6 vistas (8 brazos, ventosas biseriales, membrana interbraquial, sifón), cabeza cefalópoda y poses de alcance/extensión/rastreo.
- [04_libelula_emperador.png](04_libelula_emperador.png) — *Anax imperator* (ID 1019): adulto alado, ojos compuestos, ninfa acuática temprana (`age=0.20` sin alas adultas) y batido independiente en contrafase de las 4 alas.
- [05_medusa_ortiga.png](05_medusa_ortiga.png) — *Chrysaora fuscescens* (ID 1020): campana translúcida hueca sobre fondo oscuro y fondo claro, 24 tentáculos marginales, 4 brazos orales rizados y pulsación radial en 2 fases.
- [06_demonstration_hybrids.png](06_demonstration_hybrids.png) — Cuatro híbridos deterministas de `CreatureOffspring` (Halcón×Perro, Pulpo×Medusa, Libélula×Halcón, Mantarraya×Lagarto) probando reutilización y disciplina de homología.

---

## Defectos detectados en Pase 1 y correcciones verificadas en Pases 2–3

### 1. Halcón Peregrino (*Falco peregrinus*, ID 1016)
1. **[ALTO — Anatomía craneal/pico]** En Pase 1 el pico mostraba perfil recto con la mandíbula inferior sobresaliendo por delante de la superior y ojos amarillos caricaturescos.
   - **Corrección (`src/Head.c`, `tools/generate_five_non_mammals.py`)**: curvatura dorsal del culmen, gancho distal acusado en `HEAD_ARCHETYPE_AVIAN`, mandíbula inferior retraída bajo el pico superior con cierre limpio (`openFactor=0`) y ojos oscuros de rapaz sobre cráneo redondeado.
2. **[ALTO — Continuidad ala-torso]** En Pase 1 aparecía un hueco triangular desnudo entre el húmero y el flanco del torso.
   - **Corrección (`src/LimbAvian.c`, `src/SupplementaryMesh.c`)**: suavizado del ángulo escapulohumeral en vuelo extendido y adición de 6 plumas terciarias/escapulares que cierran limpiamente la transición ala-flanco.
3. **[MEDIO — Proporciones de torso y cola]** En Pase 1 la cola SDF sobresalía como un cilindro largo encima del abanico de plumas y el pecho caía en pico brusco.
   - **Corrección (`src/AxialBodyAvian.c`, `src/SupplementaryMesh.c`, receta 1016)**: torso en gota aerodinámica más continuo, pigostilo óseo corto y 12 rectrices largas superpuestas con barrado ventral equilibrado.
4. **[MEDIO — Animación de vuelo]** En Pase 1 las alas no aleteaban automáticamente durante el avance.
   - **Corrección (`src/ProceduralAnimator.c`)**: aleteo automático en avance (`effWingFlap`) respetando `wingFold` y anulaciones manuales.

### 2. Mantarraya Gigante Oceánica (*Mobula birostris*, ID 1017)
1. **[ALTO — Branquias ventrales y lóbulos cefálicos]** En Pase 1 los 5 pares de hendiduras branquiales eran casi invisibles y los lóbulos cefálicos resultaban demasiado estrechos.
   - **Corrección (`src/SupplementaryMesh.c`, receta 1017)**: 5 pares de hendiduras branquiales oscuras claramente posicionadas bajo el disco ventral y lóbulos cefálicos en pala más anchos y proyectados hacia delante.
2. **[ALTO — Costura ventral y coste de mallado]** En Pase 1 la aleta SDF duplicaba la superficie suplementaria causando una costura diagonal ventral y 5,05 M de celdas (~53 s).
   - **Corrección (`src/LimbFin.c`, `src/SupplementaryMesh.c`)**: uso de `BODY_CONNECTION_SUPPORT` en los tramos distales externos delegando la lámina hidrodinámica continua a `SupplementaryMesh` y ajuste tonal ventral; tiempo de mallado reducido a ~10 s con transición continua.

### 3. Pulpo Gigante del Pacífico (*Enteroctopus dofleini*, ID 1018)
1. **[ALTO — Hendidura bucal y narinas vertebradas en la cabeza]** En Pase 1 la cabeza heredaba una pequeña boca horizontal y narinas en la frente.
   - **Corrección (`src/Head.c`, `src/MonsterSDF.c`)**: supresión de narinas (`nostrilRadii = 0`) y desactivación del cortador oral frontal vertebrado en `HEAD_ARCHETYPE_CEPHALOPOD` (`entranceHalfExtents.y < 0.00095`), dejando una cabeza cefalópoda lisa con crestas supraorbitales y ojos de pupila horizontal.
2. **[ALTO — Ápice pellizcado del manto]** En Pase 1 el extremo posterior del manto terminaba en una pequeña punta.
   - **Corrección (`src/AxialBodyCephalopod.c`)**: cúpula posterior redondeada y abombada.
3. **[MEDIO — Membrana interbraquial y sifón]** Añadida membrana interbraquial muscular proximal entre los 8 brazos en `src/SupplementaryMesh.c` y sifón lateroventral más legible; onda de rastreo de brazos ampliada en `src/ProceduralAnimator.c`.

### 4. Libélula Emperador (*Anax imperator*, ID 1019)
1. **[ALTO — Cabeza con aspecto de rana y artefacto orbital]** En Pase 1 los ojos compuestos tenían pupila de diana y una hendidura roja frontal; en Pase 2 quedó una fina esquirla de cuenca orbital junto al ojo.
   - **Corrección (`src/Head.c`, `src/Creature.c`, `src/MonsterSDF.c`)**: ojos compuestos holópticos verdoso-turquesa sin aro limbal/pupila vertebrada (`orbitSocketDepth = 0` en `HEAD_ARCHETYPE_ARTHROPOD`) y supresión de boca frontal vertebrada.
2. **[MEDIO — Geometría alar y batido independiente]** En Pase 1 la venación era algo gruesa y el batido no se activaba automáticamente.
   - **Corrección (`src/SupplementaryMesh.c`, `src/ProceduralAnimator.c`, receta 1019)**: venación más fina y nítida, lóbulo anal posterior más ancho y batido en contrafase independiente para alas anteriores y posteriores; confirmada además la etapa de ninfa acuática (`age=0.20`) sin alas adultas.

### 5. Medusa Ortiga del Pacífico (*Chrysaora fuscescens*, ID 1020)
1. **[ALTO — Brazos orales dentados (low-poly)]** En Pase 1 los 4 brazos orales con 22 segmentos y alta frecuencia formaban dientes de sierra poligonales.
   - **Corrección (`src/SupplementaryMesh.c`, receta 1020)**: aumento a 72 tramos longitudinales × 5 columnas transversales con volantes orgánicos suaves y translúcidos.
2. **[ALTO — Deformación y rendimiento de la campana analítica]** En Pase 1 `AnatomyDeformer_Bind` fallaba al ser todas las aristas `SUPPORT` y el SDF sin intervalo tardaba ~25,4 s (5,98 M celdas).
   - **Corrección (`src/AnatomyDeformer.c`, `src/MonsterSDF.c`, `src/ProceduralAnimator.c`, `src/GaitPresets.c`)**: respaldo de vinculación para jaulas `SUPPORT`, cota de intervalo conservadora `MonsterSDF_MedusaBellRange` (reduciendo el mallado a **~0,12 s / 22 827 celdas**, aceleración >200×) y contracción radial visible de campana y tentáculos.

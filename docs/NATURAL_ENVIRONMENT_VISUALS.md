# Entorno natural

El estilo usa praderas oliva, tierra templada, roca neutra, sedimentos en orilla y luz fría de cielo. Los colores de cada material se calibran en su media lineal. El relieve y la costa coinciden con las superficies de mundo existentes. Las versiones físicas 1 y 2 y el formato de receta siguen siendo los mismos.

## Materiales y presentación

- El pack local `assets/terrain/natural-v2/` tiene seis familias de 512² de Poly Haven bajo CC0. El manifiesto fija ancho físico y periodo visual, autor, URL, mapas originales, orientación, formatos, huellas SHA-256, calibración y medidas de los bordes. El pack procedural anterior es el respaldo diagnosticado. Si faltan ambos packs se usan colores promedio.
- Un campo RGBA8 de 128² con mips suaviza los valores de fertilidad, humedad y distancia a orilla. La semilla de mundo aporta una variación de baja frecuencia exclusivamente visual. Las pendientes y el agua controlan la exposición de roca y los sedimentos. El mapa de biomas científico conserva sus datos originales.
- La proyección estocástica mezcla dos transformaciones. Un campo de celdas 2D, continuo y anclado al mundo, interpola la fase. Las fases enteras comparten la misma transformación a ambos lados del límite. Cada fase elige una rotación y un desplazamiento deterministas. Se rotan las derivadas para `textureGrad` y se aplica la transpuesta de la rotación a las pendientes del normal map. Los tres canales materiales usan las mismas transformaciones. Las bases de proyección convierten el gradiente al plano tangente geométrico.
- La calidad media usa microdetalle en la capa dominante. Las capas de transición aportan su media lineal y el microdetalle se desvanece al acercarse dos pesos. La proyección plana usa hasta dos muestras por mapa y seis lecturas de arrays en total. En alta, roca oblicua puede añadir dos proyecciones simples en los ejes laterales, para un máximo de doce lecturas. Se suman una lectura macro, una del contacto y el PCF. En panorámica se salta el muestreo de arrays al superar 0,134 unidades por píxel. El color límite coincide con la media lineal de los mapas. Alta extiende el umbral de panorama a 0,19.
- Las dos capas de ondas originales del pack v2 se decorrelacionan con semillas, escalas, direcciones y tiempos distintos. La amplitud se atenúa con la huella de píxel. El agua recibe sol y visibilidad, conserva Fresnel, absorción y mezcla lineal, y respeta la superficie del recorte hidrológico. La transmisión es analítica, con el fondo visible por composición alfa.
- Manchas de hierba, juncos y piedrecillas se almacenan en una rejilla visual dispersa construida por mundo. Se acota la consulta local y el frustum y se difumina la geometría cerca del límite de distancia y zoom. El presupuesto es de 2600 clumps por frame. Los recursos conservan sus modelos e identidades independientes. Las huellas locales de roca y criatura se proyectan sobre el terreno en R8 de 512². Los detalles NEAR usan suelas de stance inferidas de la pose presentada y se atenúan con la distancia a la superficie. Los cuerpos FAR usan una aproximación elíptica.

El pack v2 ocupa 18.175.300 B aproximadamente, con mips y ondas. El campo macro aporta 87.380 B y la textura de huellas 262.144 B. Los mapas de sombra y el destino auxiliar de color se informan por separado en el perfil.

```sh
python3 tools/import_natural_terrain.py --cache build/validation/scene-lighting/source-maps
python3 tools/validate_scene_lighting.py --mode captures \
  --baseline build/validation/scene-lighting/baseline
python3 tools/validate_scene_lighting.py --mode determinism \
  --baseline build/validation/scene-lighting/baseline
python3 tools/validate_scene_lighting.py --mode performance \
  --baseline build/validation/scene-lighting/baseline \
  --out build/validation/scene-lighting/profiles
```

Los flags `--no-decoration` y `--periodic-materials` aíslan la contribución de la decoración y de la proyección estocástica. `--terrain-debug=albedo|normal|roughness|weights|shadow` permite inspeccionar etapas del shader. La vista natural cambia el fondo negro por un fondo neutro en ortográfica y un gradiente de cielo en perspectiva.

## Fuentes

- [Licencia CC0 de Poly Haven](https://polyhaven.com/license)
- [Practical Real-Time Hex-Tiling](https://jcgt.org/published/0011/03/05/)

## Mediciones y evidencia

Base auditada y verificada contra `origin/main` en `89f7992e798ee2af96f8813829f1eeef72f3017f`. Se inspeccionaron ambas láminas de `terrain-biomes-proof.zip` y se regeneraron las capturas de referencia con el ejecutable de ese commit. Las parejas nuevas usan los mismos ticks, semillas, óptica de cámara, resolución y hora de agua. Los PNG, comandos, perfiles y hojas están en `build/validation/scene-lighting/`.

Equipo Ryzen 5 5600H, RTX 3050 Laptop 4 GiB, NVIDIA 580.178.04. Arena a 1920×1080, VSync desactivado, MSAA 0, seis workers, River Valley v2 semilla 42. La serie nueva mide 420 frames después de 36 frames de precalentamiento gráfico con la simulación en pausa. La base mide 420 frames después de su precalentamiento anatómico original.

| Variante | Población inicial/mín/final | P50/P95/P99 ms | FPS medio | Velocidad obtenida | GPU medio ms |
| --- | --- | --- | ---: | ---: | ---: |
| Base media 4×, panorámica (3 repeticiones) | 500/—/490 | 9,73–9,76 / 12,96–13,35 / 13,71–14,31 | 100,7–101,1 | 3,987–4,014× | 7,31–7,35 |
| Nueva media 4×, panorámica (3 repeticiones) | 500/491/491 | 14,16–14,21 / 16,62–16,82 / 16,89–17,14 | 70,5–70,7 | 3,988–3,994× | 11,73–11,85 |
| Nueva media 4×, encuadre cercano | 500/491/491 | 10,09 / 12,76 / 13,84 | 99,7 | 3,984× | 7,53 |
| Nueva media 4×, sin sombras ni huellas | 500/491/491 | 9,96 / 13,11 / 14,21 | 100,2 | 3,992× | 7,63 |
| Nueva baja 4×, panorámica | 500/491/491 | 10,21 / 13,38 / 13,98 | 97,7 | 4,000× | 7,89 |
| Nueva alta 4×, panorámica | 500/490/490 | 14,85 / 16,82 / 17,19 | 67,6 | 4,007× | 12,47 |
| Nueva media 4×, panorámica | 22/22/22 | 9,82 / 12,97 / 13,52 | 101,4 | 3,996× | 7,53 |
| Nueva media 4×, panorámica | 100/99/99 | 10,59 / 13,60 / 14,22 | 94,2 | 4,011× | 8,27 |
| Nueva media 4×, panorámica | 1000/972/972 | 18,71 / 20,82 / 22,19 | 53,3 | 3,266× | 16,41 |

En media y panorámica, la pasada solar promedia 4,10–4,11 ms, con aproximadamente 5,1 millones de triángulos al incluir todos los cuerpos FAR. Es el mayor incremento. El terreno consume 2,07 ms en panorámica (base 2,10) y 3,09 ms en el encuadre cercano (base 3,59). La beta con dos materiales estocásticos de alto detalle resultó demasiado cara de cerca y se limitó el microdetalle a la capa dominante. La tierra y el agua conservan sus mallas y LOD anteriores. El coste GPU total sube unos 4,5 ms, el P95 mediano un 28 %. Dos de las tres repeticiones quedaron por debajo de 16,7 ms en P95. La velocidad 4× se sostuvo con 500 fundadores. A 16×/max se observaron 7,40×/7,69× en esta máquina, con mínimos de población de 484. La medición de 1× obtuvo 0,987× con 500 criaturas en todo el intervalo.

Los perfiles de Arena informan media/P95 de entorno, GPU de fotograma, triángulos y comandos de sombra, coste de contacto y decoración, tiempos de CPU, huella de mundo, ticks, y RSS. La asignación calculada de entorno para media y 1080p fue aproximadamente 88 MiB, incluidos 32 MiB de sombra, 17,3 MiB de materiales y 15,8 MiB de color/profundidad auxiliar sobre el framebuffer lineal de esta sesión. El pico de RSS en dos ejecuciones calientes fue 264.264–264.268 KiB (unos 258 MiB). El primer proceso alcanzó 324.316 KiB. El presupuesto de sombra conserva todos los casters FAR, con su coste reflejado en la tabla.

### Determinismo y seguridad visual

El escenario histórico usa 16 Brumacernos, seis Velkares, 64 plantas, semilla 42, metabolismo legacy y 120 ticks. Las huellas esperadas son `78d9cc6b5e01bdde` (v1) y `cd3c91a569dcc3ba` (v2). La prueba cruzó el ejecutable headless, Arena antes y después, cámaras distintas y calidad baja/alta con sombras y decoración activadas y desactivadas. En todos los casos coincidieron los hashes históricos `77fefaaf3cc537b4` y `3dc3d76e40242214`. Los checkpoints de Lab antes y después coincidieron en huellas, población, historial y ambos cerebros campeones. La única diferencia fue el tiempo de pared transcurrido.

La prueba GL `make test-scene-lighting` compara fragmentos conocidos en órbita, profundidad dinámica, orientación de una pendiente de normal map rotada y proyección negativa. Con MSAA 0 y 4 y ambos formatos de color obtuvo visibilidad 0/255 en sombra/luz, coseno solar codificado 222, gris lineal 0,18 codificado 118 y mezcla al 50 % con blanco codificada 202. Los mismos valores se observaron con el destino lineal auxiliar. Se comprobó la restauración del framebuffer, viewport, máscara de profundidad, unidad activa y sRGB.

### Apoyo y boca

El marco de las patas de presentación ahora transporta anclas y normales por traslación, orientación completa y escala. La locomoción visual toma el marco corporal del snapshot y conserva las anclas de cada paso. El test de ladera mide una suela fija al variar escala, roll y posición. La captura mide por separado la suela estimada y la cota mínima de vértices de piel dentro de cada huella, con el estado de stance/swing. El suelo físico se muestrea antes de aplicar la oclusión. A tick 61 en River Valley v2, el máximo de hueco absoluto de suela en el Velkar adulto pasó de 0,0482 en la iteración de iluminación previa al ajuste de marco a 0,0005. El Velkar juvenil quedó en 0,0012. En Brumacerno juvenil se registró una suela swing a +0,0771, y las tres suelas stance quedaron dentro de 0,0008. Estos valores son de las tomas registradas. Los contactos de roca de la simulación siguen siendo obstáculos, y la auditoría de suela usa el heightfield.

La transferencia de agua se valida en la transacción de consumo del núcleo. En esa transacción intervienen la distancia al punto húmedo, la alineación, la velocidad y el contacto estable. La consulta bajo la proyección horizontal del centro oral es una medición separada. El registro muestra el punto de contacto, su tolerancia y la última masa realmente aceptada en el estómago molecular. Los cambios de iluminación y contacto no cambian el alcance oral ni el criterio de consumo.

La prueba de contacto de Lab Valley v2 reproduce el Brumacerno juvenil #132 al tick 1168. La proyección del punto oral físico cae en una celda seca. El punto objetivo húmedo pertenece al cuerpo 6, a 0,00856 unidades del punto oral frente a una tolerancia de 0,05607. Se aceptaron 0,007500 unidades de masa en ese mismo tick. El Velkar juvenil #136, tick 3968, tenía ambos puntos en el cuerpo 5 y aceptó 0,008333 unidades. La prueba conserva esta semántica histórica de contacto por distancia. No mide un segundo punto anatómico independiente en el labio.

El Brumacerno adulto #125 en bebida, tick 4928, mantiene una penetración de suela de −0,2838 en un apoyo delantero. El Velkar juvenil #136 registra hasta −0,1079 en un apoyo. El mapa local descarta las huellas demasiado separadas de la superficie. La mejora en marcha no certifica los apoyos de todo el rango de posturas de bebida.

```sh
python3 tools/validate_scene_lighting.py --mode contacts \
  --baseline build/validation/scene-lighting/baseline \
  --out build/validation/scene-lighting/contact-release
```

`make test`, `make editor-test`, `make validate-procedural-world`, `make validate-ecosystem`, `make world-sanitize`, `make world-tsan` y `make test-scene-lighting` pasaron. También se completaron los benchmarks de mundo, escalabilidad y metabolismo molecular. A 500 criaturas sostenidas, el benchmark de mundo v2 midió 1,540/2,266 ms (media/P95) en la base y 1,546/2,280 ms en la revisión. Las seis ejecuciones alternadas de 1200 ticks en configuración molecular con 500 fundadores terminaron con 498 criaturas y hash `d1358e8278a77dd2`. Los tiempos medios de proceso por tick estuvieron en 3,821–3,843 ms en la base y 3,788–3,808 ms en la revisión.

Se capturaron Arena y Lab, respaldo procedural v1 del pack, respaldo sin texturas, un morph de estudio y ambos perfiles de agua. La revisión valida una selección acotada de poses y cámaras. Quedan pendientes el contacto en posturas extremas a través de todos los estadios, reducir el coste de profundidad en panorámicas y una prueba prolongada de cambios de mundo y tamaño de viewport en los demos interactivos.

## Revisión de transporte y relieve v3

La luz natural actual usa el mismo modelo solar dieléctrico para criaturas y suelo, con lóbulos direccionales para el manto, oclusión de reposo y rebote de baja frecuencia derivado del material próximo. Las recetas incluidas usan el generador v4 y comparten el terreno físico entre Arena y Lab. `--hide-water --terrain-lod0 --terrain-debug=slope` permite revisar el talud y su geometría sin composición de agua.

`--capture-state` y `--capture-species` verifican la identidad solicitada. La captura en luz natural audita la profundidad del cuerpo resuelto, la caja proyectada, la ruta de piel y los apoyos disponibles. El validador compara huella de mundo, hash de eventos y tick entre tomas pareadas de criatura. El informe documenta qué capturas faltan y las limitaciones de la prueba de profundidad.

- [Registros, pruebas y ejemplos](WORLD_MORPHOLOGY_LIGHT_TRANSPORT_V3.md)

En la captura LOD0 de orilla v3 se aisló el patrón de bandas con la visualización de visibilidad y con el sol sin sombras. El sesgo de plano receptor resolvió la banda reproducida en esa cámara, manteniendo las sombras reales.

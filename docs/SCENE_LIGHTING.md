# Iluminación de escena

Evolution Arena y Brain Evolution Lab comparten una escena de presentación diurna para mundos procedurales. El backend es OpenGL de compatibilidad con shaders GLSL 330. La simulación headless y los evaluadores de Lab siguen en C sin dependencia del backend gráfico. Los mundos planos y las herramientas de estudio conservan el modo de iluminación anterior.

## Renderizado

1. Arena adquiere el snapshot publicado y Lab prepara el estado de replay en su hilo gráfico. Seleccionan LOD y actualizan las poses antes de grabar la lista de dibujo. Los comandos retienen las mallas durante el fotograma y copian las instancias, matrices, paletas de pose y overrides de superficie que necesitan.
2. `EnvironmentRenderer_RenderScene` construye la proyección solar, carga profundidad estática o la reutiliza, y reproduce las órdenes opacas relevantes. La profundidad usa las mismas matrices de modelo, geometría de piel, cabezas y mandíbulas. En instancing reutiliza el vertex shader. El fallback CPU conserva la malla deformada del fotograma. En GPU la salida del cómputo se reutiliza si ninguna otra pose compartida ha escrito ese búfer. Se excluyen ojos, pelo y agua de profundidad.
3. El bloque `SceneLightingFrame` suministra luz directa y ambiental, proyección de sombras, vista inversa, posición de la cámara y exposición. El terreno, las rocas y el agua trabajan en coordenadas de mundo. Los seres y los recursos reciben el sol rotado a vista con w=0. Para sombras, niebla y luz ambiental se reconstruye mundo desde la posición de vista. Las posiciones de reposo, pigmentación y propiedad de morph se mantienen en su espacio propio. La dirección de vista es constante en ortográfica.
4. La lista de color dibuja recursos, criaturas y decoración después del entorno opaco. El agua compone después con el depth test de la geometría que ya se ha dibujado. El backend devuelve el estado de composición antes de overlays, minimapa y HUD.

Las superficies de piel, queratina, ojos, pelo y agua mantienen respuestas distintas. La visibilidad solar atenúa el término directo. La luz celeste y el rebote del suelo permanecen en sombra. El AO de material y la huella de contacto atenúan la componente indirecta del terreno. La vista de estudio tiene su propio bloque neutro y no depende del último mundo dibujado.

### Color

El albedo del terreno se decodifica por hardware mediante `GL_SRGB8_ALPHA8`. La pigmentación de recetas, los colores de malla y las imágenes de iris se interpretan como datos de autor en sRGB al entrar en la respuesta natural. Normales, roughness, profundidad y pesos quedan lineales. El framebuffer sRGB convierte una sola vez al almacenar y mezcla en espacio lineal. Para un destino de color lineal, la escena usa renderbuffers sRGB del mismo tamaño y número de muestras, con copia de color codificado y profundidad al destino del llamador. El HUD usa sus colores anteriores después de restaurar `GL_FRAMEBUFFER_SRGB`. No hay un pase HDR permanente.

La prueba GL comprueba el resultado del gris lineal 0,18 y una superposición semitransparente, la visibilidad de una superficie conocida, la constancia del coseno solar bajo órbita y la restauración de estado. Repite la prueba con FBO sRGB y lineal, en perspectiva y ortográfica, a MSAA 0 y 4.

### Profundidad solar

| Calidad de sombras | Resolución | Filtro | Presupuesto para dos mapas D24 |
| --- | ---: | --- | ---: |
| `off` | — | — | 0 al iniciar |
| `low` | 1024² | Comparación bilineal | 8 MiB |
| `medium` | 2048² | Cuatro comparaciones bilineales | 32 MiB |
| `high` | 2304² | Cuatro comparaciones bilineales | 40,5 MiB |

La representación de memoria estima 4 B por texel D24. La calidad general baja elige sin mapa por defecto, media y alta eligen el mapa correspondiente. Se puede fijar `--shadows` por separado. Se conserva un mapa de terreno y roca por época y matriz solar ajustada a texels. Se copia su profundidad al destino dinámico y se añaden recursos y criaturas de la lista de presentación, con una única consulta de visibilidad resultante. La proyección incluye toda la profundidad de los posibles oclusores estáticos a lo largo del sol. En panorámica, cubre todo el terreno. En encuadres cercanos, se ajusta a un área focal escalonada y estabilizada sobre la cuadrícula de texels. Fuera de la cobertura focal se usa luz directa sin oclusión. Las criaturas de fuera del frustum visual no entran en la lista de presentación. Una roca estática fuera de cámara sí puede proyectar sombra dentro de ella.

El sesgo usa `glPolygonOffset` y una pequeña compensación del receptor basada en la normal geométrica. El LOD de terreno de la pasada de sombras se elige por error proyectado y mantiene las costuras. Cambiar de época, ángulo solar, tamaño o proyección invalida la profundidad estática. Las consultas de tiempo usan resultados diferidos. Leer un mapa de profundidad sólo ocurre al solicitar `--capture-shadow`.

## Reproducción

```sh
make -j8 demos/demo_evolution_arena demos/demo_brain_evolution_lab test-scene-lighting
demos/demo_evolution_arena --world=worlds/river_valley_v2.world.json \
  --world-seed=42 --seed=42 --quality=medium --shadows=medium --sun=137.5,47 \
  --brumacernos=400 --velkares=100 --plants=700 --capacity=640 --workers=6 \
  --width=1920 --height=1080 --zoom=160 --view=three-quarter \
  --frames=60 --step-lock --water-time=.75 --no-hud --check-gl \
  --capture=build/validation/scene-lighting/valley.ppm \
  --capture-shadow=build/validation/scene-lighting/valley-shadow.pgm
```

`--shadows=off` conserva las huellas locales. `--no-contacts` permite observar sólo el mapa solar. Con ambos flags se puede medir la referencia sin oclusión. `--terrain-debug=shadow` muestra la visibilidad directa sobre el terreno. En Lab están disponibles también `--quality`, `--sun`, `--no-decoration`, `--no-hud` y `--check-gl`.

`tools/validate_scene_lighting.py` reproduce escenarios y comparaciones a ticks fijos. El perfil de Arena admite `--profile-warmup=N` para preparar los recursos con la simulación en pausa y medir después de la carga. Los percentiles GPU se calculan con resultados diferidos recuperados durante la ejecución. Los triángulos de objetos informados corresponden a la geometría base y no suman por cada capa transparente de pelaje.

## Referencias

- [Reglas de codificación y mezcla de framebuffer de Khronos](https://wikis.khronos.org/opengl/Framebuffer)
- [Estabilidad y filtrado de mapas direccionales](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)

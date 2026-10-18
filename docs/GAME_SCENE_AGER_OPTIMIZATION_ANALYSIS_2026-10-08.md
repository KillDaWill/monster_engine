# Análisis y estrategia de optimización del Ager en la escena del juego

8 de octubre de 2026. Estado: análisis de código y medición; las optimizaciones propuestas todavía no están implementadas.

La caída al pulsar T procede principalmente de recompilar estructuras de contacto y certificar el campo SDF en CPU, con extracción GPU adicional. La interpolación semántica de MonsterAger representa una fracción pequeña del fotograma. El primer cambio concreto debería eliminar las búsquedas repetidas del recorrido axial en MonsterSDF_Build; después hay que convertir el contacto y las consultas conservadoras en actualizaciones de estructuras persistentes.

## Método y alcance

Se usó el ejecutable existente de este checkout, que `make -n demos/demo_game_scene` considera actualizado. El árbol contiene cambios previos; sus fuentes y el ejecutable quedan identificados mediante SHA256 en `metadata.json`. No se modificó el código funcional ni se recompiló el ejecutable principal. Dos ejecutables adicionales se enlazaron en /tmp sustituyendo únicamente un objeto por una copia instrumentada de su fuente actual, compilada con gcc, -O2, -Wall, -Wextra y -Wpedantic. Los parches de instrumentación se conservan para revisar las mediciones.

GPU verificada en los logs: NVIDIA GeForce RTX 3050 Laptop, driver 580.173.02. Ventana 1200 × 850, MSAA 4, pelaje activo, voxel SDF 0,075 y dos pasos de refinamiento. Semilla 42. Diez destinos desde Emerald Climber, 300 fotogramas cada uno, recorridos secuenciales, sin compilaciones ni otras demos en paralelo. La medición basal usa 240 fotogramas sin mutación. Se conserva la receta completa de cada variante.

La opción headless automatiza T en el fotograma 30; sigue creando una ventana y renderizando. La simulación avanza 1/60 s por fotograma y la mutación recorre tres segundos simulados. Se desactiva el límite de presentación únicamente para medir. La ventana principal de análisis es 35–208, 174 muestras por destino; excluye preparación inicial, la pulsación y la liberación final. El CSV conserva esos otros fotogramas. El estado asentado usa 230–299. La referencia sin mutación usa 30–239. Los percentiles interpolan linealmente entre muestras ordenadas.

Los informes antiguos sobre AMD Renoir y variantes sin todos sus accesorios no se usan como medición actual. Los resultados siguientes son una ejecución por destino, un origen y un encuadre; no certifican todos los pares de variantes, paredes o recorridos interactivos.

## Resultados actuales

Sin mutación: 9,86 ms medios y 12,53 ms en p95, sin límite de FPS. Hay margen para presentar a 60 FPS. Durante la mutación, todas las variantes medidas superan el presupuesto medio de 16,67 ms.

| Destino | Media del fotograma ms | FPS equivalentes | p95 ms | Contacto: compilar ms | Certificación y páginas ms | Construir SDF ms | GPU lagarto ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| Emerald Climber | 25.13 | 39.8 | 29.53 | 8.93 | 5.24 | 0.23 | 6.63 |
| Desert Horned | 45.07 | 22.2 | 50.65 | 15.22 | 9.71 | 5.39 | 10.70 |
| Blue Runner | 22.50 | 44.4 | 31.54 | 5.88 | 4.16 | 0.23 | 6.73 |
| Fat-Tailed Gecko | 29.52 | 33.9 | 33.88 | 9.54 | 7.01 | 0.22 | 8.10 |
| Volcanic Spiny | 30.87 | 32.4 | 33.28 | 10.05 | 7.24 | 0.63 | 9.04 |
| Sand Burrower | 24.24 | 41.2 | 27.11 | 8.04 | 4.33 | 0.25 | 6.05 |
| Stone Armored | 29.76 | 33.6 | 32.70 | 9.50 | 7.29 | 0.32 | 7.95 |
| Sailback | 33.32 | 30.0 | 37.42 | 9.98 | 10.06 | 0.71 | 9.54 |
| Jewel Chameleon | 28.66 | 34.9 | 31.59 | 9.38 | 6.40 | 0.26 | 7.75 |
| Aquatic Fin-Tail | 25.64 | 39.0 | 27.62 | 8.31 | 5.08 | 0.23 | 6.66 |

Los tiempos de contacto, SDF e interpolación están incluidos en update_ms. La certificación está incluida en upload_ms y draw_cpu_ms. No deben sumarse otra vez a esos totales. Los cronómetros del hilo principal miden tiempo transcurrido, que puede incluir espera del driver. Las consultas GPU son diferidas y corresponden a timing_generation; gpu_ms sólo cubre la superficie dispersa del lagarto, no el perro ni todo el escenario. Su intervalo total incluye etapas auxiliares y huecos entre comandos.

En Sailback: 15,85 ms en actualización y 17,07 ms en la llamada de dibujo. Ager cuesta 0,43 ms y copia/reconfiguración de rig 0,34 ms. Recompilar el contacto cuesta 9,98 ms, frente a unos 0,02 ms una vez asentada la anatomía. El solver articulado cuesta aproximadamente 0,04 ms en este recorrido interior; no es la misma operación que compilar sus soportes. La deformación CPU del perro cuesta unos 3,39 ms; el lagarto no usa ese binding CPU.

## Qué ocurre al pulsar T

1. demo_game_scene.c:817–884 selecciona la variante, aplica su receta completa, construye el destino, clona el origen y crea Ager. Avanza temporalmente al extremo adulto y vuelve a cero para registrar los dominios de accesorios. Es preparación síncrona de la pulsación, distinta del coste sostenido.
2. En cada fotograma activo, :915–940 avanza la edad suavizada, resuelve fenotipo/anatomía/material, copia el resultado al actor, construye el rig y configura la animación conservando los apoyos.
3. :967–983 construye el SDF y compila los soportes conservadores de contacto. Cada cambio geométrico invalida la huella del contacto.
4. :433–468 actualiza navegación, locomoción/IK y respuesta articulada. La pose final se calcula una vez por fotograma. El perro deforma su malla CPU; el lagarto usa pose GPU.
5. :1079–1084 reconstruye y sube dominios de material/pose y llama al renderer sparse. Este compara features, marca bricks modificados, certifica interiores/exteriores y prepara páginas de muestras.
6. La GPU libera páginas/aristas afectadas, comparte y evalúa esquinas, clasifica celdas, asigna páginas, emite índices, refina raíces y normales, actualiza material y dibuja. El fence certifica la generación completada antes de reutilizar recursos.

Fuera de Ager se conservan geometría y contacto mediante huellas. Durante el cambio de todo el cuerpo, muchos bricks están realmente modificados; ser sparse no implica que haya poco trabajo. Sailback modifica una media de 1.453 bricks por fotograma, de los que unos 965 se certifican sin superficie; quedan unas 328.000 evaluaciones únicas de esquinas. Los halos ya comparten muestras, por lo que volver a proponer esa deduplicación no añade una optimización nueva.

## Atribución adicional con instrumentación

La instrumentación independiente del renderer para Sailback separa el tramo etiquetado certify_ms:

| Operación | Media ms |
|---|---:|
| Espera y comprobación del fence anterior | 0,041 |
| Actualizar metadatos/features/bricks | 0,299 |
| Subir descripción SDF | 0,023 |
| CertifySparseJobs: intervalos conservadores | 9,944 |
| Preparar/subir páginas de esquinas | 0,085 |
| Enviar toda la operación sparse, excluido fence previo | 10,682 |

El gasto de certificación en este caso procede esencialmente de evaluar intervalos CPU; ni la transferencia de la descripción ni el fence explican esos diez milisegundos. No se atribuyen a otras GPU o cargas.

La copia instrumentada de MonsterSDF_Build para Desert Horned observa 315 nodos, 314 conexiones y 310 conectores SDF. El recorrido previo axial consume el 98,1 % de su tiempo medido: 9,28 de 9,46 ms en ese ejecutable de diagnóstico. Estos tiempos absolutos difieren de los 5,39 ms del ejecutable principal y no deben sustituir sus cifras ni utilizarse como comparación antes/después. La atribución por fases localiza el problema en la búsqueda estructural, con sólo unos 0,16 ms en compilar conectores y 0,02 ms en cabeza/bounds.

En src/MonsterSDF.c:229–249, por cada nodo axial se recorren todas las conexiones y se llaman dos búsquedas lineales AnatomyGraph_FindNode antes de filtrar BODY_CONNECTION_AXIAL_LOFT. Ese trabajo se realiza incluso antes de usar un axialProfile ya disponible. El coste crece con los ornamentos aunque esos ornamentos no formen parte de la cadena axial.

perf no permite muestrear eventos en esta máquina: perf_event_paranoid=4. Se usaron cronómetros por fases y estas copias instrumentadas, sin cambiar permisos del sistema.

## Experimento: coste de la certificación frente al muestreo

Se ejecutó otra mutación Sailback con MONSTER_SPARSE_NO_RANGE_CERTIFICATION=1, manteniendo voxel, MSAA, accesorios, refinamiento y pelaje.

| Métrica | Con certificación | Muestreando todos los bricks afectados |
|---|---:|---:|
| Media del fotograma ms | 33,32 | 29,23 |
| p95 del fotograma ms | 37,42 | 37,98 |
| Certificación y páginas ms | 10,06 | 0,32 |
| Muestreo GPU ms | 4,34 | 14,98 |
| Intervalo GPU sparse ms | 9,54 | 21,32 |
| Esquinas únicas, media | 327.794 | 840.763 |

La media mejora aproximadamente un 12 %, pero el p95 no mejora y el trabajo GPU aumenta mucho. Esta ejecución aislada no establece una política universal ni certifica equivalencia visual. La opción mantiene el algoritmo de muestreo completo, pero aquí no se ejecutó una comparación de imágenes/signaturas ni readbacks de validación. Es una prueba de coste, no un cambio recomendado como solución definitiva.

## Estrategia de implementación, por orden

### 1. Eliminar las búsquedas redundantes de construcción SDF

Primera intervención pequeña y medible en MonsterSDF_Build:

- Filtrar el tipo de conexión antes de buscar extremos. Preservar la validación de conexiones mediante un pase explícito o una compilación estructural validada.
- Resolver una vez los índices from/to por identidad y los grados/adyacencias axiales. Actualizar coordenadas/radios sin volver a descubrir la cadena cada edad.
- Consumir el perfil axial ya resuelto antes de ejecutar la ruta de descubrimiento alternativa; mantener las verificaciones estructurales necesarias.
- Conservar el orden de conectores y uniones suaves. Reutilizar el árbol de conectores y actualizar sus bounds desde las hojas cuando la topología es compatible.
- Reutilizar el SDF entero cuando geometría y parámetros de cavidades/mandíbula no cambien; cambiar sólo la receta de superficie cuando cambie el material. Hoy se construye incluso fuera de la mutación, y Desert Horned sigue gastando unos 5,49 ms por fotograma ya asentado.

La mejora de esta intervención está por medir. El total SDF de cada variante marca el techo de ahorro directo; no se debe extrapolar el coste de Desert Horned a todos los destinos.

### 2. Hacer persistente la compilación del contacto

CreatureBoundary_BindSDF recompila el dominio, certifica 216 grupos y hasta 1.728 celdas, compara candidatos de skinning y reconstruye soportes por joint. La huella actual incluye la anatomía completa: cualquier cambio continuo fuerza ese recorrido. El solver posterior ya tiene una salida barata cuando no hay contacto.

Separar identidad/topología de los parámetros geométricos. Conservar asociaciones nodo/conexión/joint, listas de celdas y contribuciones por joint; actualizar sólo lo afectado. Una retícula de contacto estable durante el ciclo facilita reutilizar clasificaciones, frente a las celdas actuales que cambian al variar sdf->bounds. Las contribuciones deben poder retirarse cuando una celda deje de contribuir; acumular cajas indefinidamente produciría un contacto cada vez más laxo.

Usar una jerarquía espacial conservadora para certificar grupos grandes y bajar a celdas/candidatos sólo si es necesario. Las cotas temporales pueden reutilizar una clasificación entre edades únicamente cuando un intervalo certificado incluya todos los parámetros de ese tramo. Si no hay prueba, recalcular a la edad actual. Los extremos solos no certifican todas las edades y una envolvente excesiva alteraría navegación/giros. Mantener ojos, mandíbula, ornamentos y margen de pelaje en la envolvente articulada.

Este es el mayor ahorro CPU recurrente: aproximadamente 6–15 ms medios según destino. No equivale a esa cantidad de mejora del fotograma completo, por solapamiento y costes compartidos.

### 3. Reducir el coste de los intervalos para extracción

CertifySparseJobs consulta el campo completo por cada brick modificado; MonsterSDF_GetCellRangeWrapper resuelve barridos, conectores, uniones y cavidades. Hay aceleración por árboles y salidas positivas, pero siguen siendo cientos/miles de consultas independientes por edad.

- Compilar coeficientes Hermite, radios recíprocos, marcos y dependencias una vez por versión geométrica.
- Consultar jerárquicamente grupos de bricks: una prueba de intervalo suficientemente fuerte certifica todos los hijos. Descender cuando el resultado sea incierto.
- Compartir la descripción de aceleración y cotas con contacto, material y pose. Las rejillas de contacto/extracción son distintas, por lo que sus resultados por caja no se pueden reutilizar indiscriminadamente.
- Comparar una certificación GPU conservadora, con descarte barato y muestreo completo para lo incierto. Es el camino para conservar el ahorro de muestras sin pagar toda la aritmética en el hilo principal. Los márgenes de redondeo y las cavidades necesitan validación; no basta traducir las operaciones float y asumir conservación.

No eliminar una celda por tener ocho esquinas del mismo signo: las superficies finas interiores podrían desaparecer.

### 4. Reducir reconstrucciones estructurales y acotar evaluación GPU

La demo reconstruye el dominio de material, el dominio de pose y el dominio de contacto. Cada uno busca nodos/joints y reconstruye estructuras de aceleración. Cachear correspondencias por identidad y el árbol; actualizar parámetros y refitar bounds. Las referencias de material y de reposo tienen propósitos diferentes: compartir índices/topología no significa sustituir sus coordenadas por las mismas.

La caché sparse ya distingue material y geometría en varias huellas. Sin embargo, CollectSurfaceFeatures usa un máximo global de smoothness en soportes y fingerprints de muchos elementos. Revisar si se pueden registrar dependencias conservadoras de las uniones locales para evitar invalidaciones ajenas al cambio; no estrechar esas regiones sin probar el soporte completo de la composición SDF.

En GPU, sample/root/material activan la escena completa y ya podan grupos/conectores por cotas. Compilar candidatos conservadores por brick para evitar recorrer los mismos grupos lejanos en cada muestra. Incluir halos de normales, refinamiento, cavidades y regiones afectadas por uniones suaves, conservando el orden CSG. Un recorte discontinuo del campo por AABB no sustituye esa prueba.

root.comp realiza dos pasos de raíz y, con cachedNormals desactivado en esta demo, cuatro evaluaciones adicionales de distancia por normal. Una evaluación conjunta de esas cuatro muestras podría compartir cargas y cotas sin cambiar la fórmula. Evaluar gradientes analíticos completos sería una evolución mayor; interpolar normales sin validación no es una optimización equivalente.

### 5. Separar preparación/extracción de dibujo para solapar trabajo útil

Actualmente el renderer mezcla planificación CPU, extracción y draw. Separar la preparación del trabajo, la ejecución compute y la presentación permite enviar el SDF de reposo de la edad actual antes de resolver la pose/contacto final, y dibujarlo después con esa misma edad y pose. Las dependencias reales de mandíbula/field deben estar resueltas antes del envío.

Esto puede solapar GPU con contacto/animación/deformación CPU. Reordenar llamadas no elimina los 20 ms de cálculo CPU; esta etapa es más útil después de reducir las consultas conservadoras. Mantener barreras, comprobación de overflow y commit tras fence. No aumentar la edad de la geometría presentada ni usar geometría obsoleta para aparentar rendimiento.

### 6. Recuperar margen del coste basal y de la pulsación

El perro deforma unos 85.000 vértices en CPU y RenderSurfaceMesh vuelve a empaquetar/subir posiciones y normales. Skinning GPU persistente con la misma fórmula/influencias puede recuperar parte de los 3–4 ms CPU y las transferencias, manteniendo coherencia con pelaje/guards y contención CPU. Es una mejora del coste basal, no la causa principal del pico de Ager.

MonsterAnimation_Configure asigna una nueva estructura, reinicializa animador y remapea joints cada edad. Una actualización del rig compatible en el mismo estado evita asignaciones y búsquedas; mantener fases, pies plantados y rotaciones. Su coste actual de 0,27–0,68 ms lo coloca detrás de los problemas anteriores.

Para el instante de T, medir por separado aplicar receta, construir destino, clones, creación de Ager y registro del material. Reservar capacidades y compilar la correspondencia origen/destino una vez por transición puede eliminar trabajo repetido. Esa preparación estructural también simplifica las interpolaciones posteriores; no hace falta retrasar el inicio visible ni trasladar remallados completos a otro hilo.

## Verificación que debe acompañar la implementación

Objetivo: trabajo CPU/GPU dentro del presupuesto de 16,67 ms en p95/p99 con calidad actual, y presentación estable alrededor del límite de 60 FPS; documentar explícitamente la parte que siga fuera del presupuesto. El limitador actual redondea SDL_Delay hacia arriba, por lo que el intervalo presentado puede superar ligeramente 16,67 ms aunque el trabajo quepa. Instrumentar GPU del frame entero además del lagarto, separar preparación T, espera del fence y swap, y conservar la asociación de generaciones de la telemetría.

Validar primero cada intervención contra la ruta original: anatomía/SDF, rig y apoyos, soportes de contacto, posiciones/normales y material. Probar edades intermedias, inversión y varias mutaciones consecutivas. Incluir las diez variantes completas, vistas cercanas y paredes. El muestreo de referencia y las lecturas de diagnóstico se ejecutan aparte del benchmark.

Si se cambian interfaces compartidas, realizar make -B test demos; regenerar headers de shaders con los scripts del proyecto. Las rutas CPU/GPU de pose y contención requieren comprobaciones separadas. No se han ejecutado esas pruebas en este análisis ni se afirma que las optimizaciones estén validadas.

## Reproducción y evidencia

```sh
./demos/demo_game_scene --headless --benchmark-uncapped --frames=240 --cycles=0 --seed=42 --profile=/tmp/baseline.csv
./demos/demo_game_scene --headless --benchmark-uncapped --frames=300 --target=1 --seed=42 --profile=/tmp/horned.csv
./demos/demo_game_scene --headless --benchmark-uncapped --frames=300 --target=7 --seed=42 --profile=/tmp/sailback.csv
MONSTER_SPARSE_NO_RANGE_CERTIFICATION=1 ./demos/demo_game_scene --headless --benchmark-uncapped --frames=300 --target=7 --seed=42 --profile=/tmp/sailback-no-cert.csv
```

[CSV, logs, hashes, resúmenes y parches de instrumentación](validation/game-scene/ager-analysis-20261008/).

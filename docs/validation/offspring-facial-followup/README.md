# Seguimiento facial y caudal de CreatureOffspring

Se reprodujeron diez recetas guardadas por pareja con los binarios anterior y actual. Después se generaron diez crías por pareja y semilla con `48216` y `21473`. La repetición exacta muestra el efecto de los resolutores sobre la misma receta. Las galerías de crías nuevas reflejan también los cambios en la selección de candidatos.

## Cerdo × Elefante

El dominio de material de la trompa era el de la cabeza entera. Los bancos de pigmentación corporal de estos progenitores excluyen esa región y la pigmentación local de la cara pierde influencia más allá del morro. La cadena nasal conserva el dominio facial en la inserción y adopta el dominio corporal en el tramo libre, tanto en el mapeo de malla como en el SDF de render. [Comparación de piel y silueta](pig-elephant-replay.jpg)

La longitud mínima de una extensión heredada depende de la altura del apoyo facial y del radio terminal. En la repetición exacta de la cría #10, la longitud de arco pasó de 0,040 a 0,446 unidades de escena. Las dos narinas dieron distancia positiva y el tabique dio distancia negativa en las muestras de los 16 descendientes con trompa de las dos tandas nuevas. Los cuatro restantes conservan el morro corto. Entre las extensiones heredadas se observaron longitudes de 0,386 a 2,616. [Semilla 48216](elephant-48216.jpg) · [Semilla 21473](elephant-21473.jpg) · [Medidas nasales](nasal.tsv)

El máximo de flexión al enrollar y al llevar el ápice a la boca depende de la relación entre longitud y grosor. La prueba de SDF sigue cinco puntos de salida de cada narina durante ambos gestos en las extensiones cortas muestreadas. Los renders de malla animada muestran algunos bordes triangulados alrededor del labio y de los extremos del colmillo. [Reposo, enrollado, alcance y alimentación](pig-elephant-poses.jpg)

## Caballo × Cerdo

- **Colas.** El ensanchamiento de la pelvis se propagaba al inicio de la cola independiente y formaba un bulto que ocultaba el rizo. Se limita el diámetro del pedículo con la propia sección caudal. La cola colgante parte más atrás, en la dirección de su anclaje, para que el maslo salga de la grupa. El pelo largo anclado y los mechones antiguos por región requieren una cola de la misma familia anatómica durante la herencia. Las dos tandas produjeron colas en espiral y colas colgantes. [Repetición exacta](horse-pig-replay.jpg) · [Pelo y marcha](horse-pig-hair.jpg)
- **Morro.** Se aisló el bulto de la cría #2 modificando un componente a la vez en copias de su receta. El maxilar quedaba asentado en el centro, pero parte del volumen sobresalía por delante y por encima del puente inclinado. Se ajustan su posición y sus radios según el volumen de apoyo. La almohadilla nasal también toma sus límites de la última sección del morro cuando se combinan un disco nasal y un perfil alargado. [Semilla 48216](horse-48216.jpg) · [Semilla 21473](horse-21473.jpg)

La validación de generación produjo diez recetas para cada una de las 66 parejas. Las tres repeticiones adicionales de la semilla `48216` produjeron archivos idénticos para las dos parejas investigadas. Pasaron `make test` y `make visual-morph-gpu-regressions`. En algunos morros alargados aún se aprecian pliegues alrededor del ojo y comisuras pronunciadas. Los cambios de geometría y pigmento se aplican al cargar recetas anteriores. La selección de pelo compatible se aplica al generar crías, por lo que los mechones ya grabados en recetas antiguas conservan su asignación. [Matriz de generación](regression.csv)

## Coste de generación

Mediana de tres procesos por binario y pareja, fijados a la CPU lógica 9 con la semilla `48216`. El tiempo del exportador corresponde a la generación de diez crías y el RSS al pico del proceso completo.

| Pareja | Tiempo anterior → actual | Propuestas evaluadas | Pico RSS anterior → actual |
| --- | ---: | ---: | ---: |
| Caballo × Cerdo | 278,4 → 348,2 ms | 65 → 77 | 9976 → 9840 KiB |
| Cerdo × Elefante | 288,8 → 290,0 ms | 60 → 60 | 10016 → 9920 KiB |

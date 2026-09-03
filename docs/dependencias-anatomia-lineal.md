# Auditoría de dependencias de anatomía lineal

Esta auditoría describe las dependencias encontradas antes de migrar el cuerpo a `AnatomyGraph`. La fase de cabeza no las elimina: las encapsula y evita añadir otras nuevas.

## Orden como conectividad o identidad

- `src/MonsterSDF.c`: crea exactamente `bodyPartCount - 1` conectores y une `bodyParts[i]` con `bodyParts[i + 1]`. Es la dependencia geométrica principal.
- `src/MonsterQueries.c`: `Monster_GetPartWidth`, `Monster_GetPartHeight`, `Monster_GetDirection`, `Monster_GetDirectionFromNextPart` y `Monster_GetPosition` interpretan vecinos de array como vecinos anatómicos; `Monster_GetTotalLength` suma la cadena por orden.
- `src/Monster.c`: `Monster_GetHead` define la cabeza como `bodyParts[0]` y `Monster_SetAngleTarget` usa esa convención.
- `src/MonsterAger.c`: iguala cantidades añadiendo secciones al final e interpola `bodyParts`, `eyes` y `mouths` por índice. La nueva ruta `Head` sólo evita esa identidad posicional para el fenotipo de cabeza.
- `src/MonsterQueries.c`: `Monster_GetNonWiggleCenter` se detiene al encontrar el primer rasgo ondulante, por lo que supone que la cola ocupa un sufijo contiguo.
- Demos y pruebas: construyen cabeza, pecho, abdomen y cola en orden y adjuntan ojos/boca mediante `bodyPartIndex`.

## Anclajes faciales heredados

- `Eye.bodyPartIndex` y `Mouth.bodyPartIndex` seleccionan el anfitrión por índice.
- `MonsterVisual.c` y `MonsterVisualAsync.c` convierten esos offsets a mundo y mantienen arrays paralelos por índice.
- `MonsterSDF.c` conserva esos arrays como puente, pero cuando `Monster.hasHead` está activo deriva cráneo, rostro, órbitas, narinas y oral system desde `HeadPhenotype`; los campos craneales heredados de `Mouth` dejan de ser la autoridad.

## Terreno

- `src/BodyPart.c`: cada `BodyPart_Update` consulta `Monster_GetWorldHeight` con sus propias coordenadas X/Z y reemplaza su Y por altura más `groundOffset`.
- `src/MonsterQueries.c`: delega la consulta en `World.getWalkingHeight`.

Esto debe migrarse en locomoción: sólo efectores de contacto consultarán terreno; el torso y la cabeza heredarán la pose resuelta.

## Corte recomendado para AnatomyGraph

Introducir primero nodos axiales `ROOT -> PELVIS -> SPINE -> NECK -> HEAD`, con `AnatomyId`, `parentId`, rol, transform de reposo, longitud, grosor y límites. Mantener un adaptador temporal a `BodyPart[]`; cambiar después `MonsterSDFConnector` para consumir aristas del grafo. No añadir miembros hasta que ese camino axial reemplace de forma verificable la conectividad `i -> i + 1`.

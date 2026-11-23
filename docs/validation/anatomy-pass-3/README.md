# Tercer refinamiento anatómico: canguro rojo, ornitorrinco y jirafa

Comparación directa contra la revisión base `ce47398842e17595c780bc62bf07cf99faaa7436` conservando cámara ortográfica, centro, escala, orientación e iluminación mediante `MONSTER_CAPTURE_CAMERA_REFERENCE`.

| Especie | Adulto (6 vistas, arcilla y superficie) | Detalles antes/después | Desarrollo (4 etapas + 0,25/0,50/0,75) | Animación y poses |
| --- | --- | --- | --- | --- |
| Canguro rojo (1014) | [Adulto](canguro-adulto.jpg) | [Mano, hombro, pie y suela](canguro-detalle.jpg) | [Desarrollo](canguro-edades.jpg) | [Poses](canguro-poses.jpg), [ciclo de salto](canguro-ciclo.jpg) |
| Ornitorrinco (1012) | [Adulto](ornitorrinco-adulto.jpg) | [Bóveda, pico, ojos y mano](ornitorrinco-detalle.jpg) | [Desarrollo](ornitorrinco-edades.jpg) | [Reposo y marcha](ornitorrinco-poses.jpg) |
| Jirafa (1011) | [Adulto](jirafa-adulto.jpg) | [Osiconos y orejas](jirafa-detalle.jpg) | [Desarrollo](jirafa-edades.jpg) | [Reposo, marcha y bebida](jirafa-poses.jpg) |

## Resumen de correcciones

- **Canguro rojo (1014):** `LimbMammal.c` añade la estación intermedia del metatarso (`212`) para eliminar el cilindro rígido y el abultamiento distal, fundiendo el metatarso ahusado con el cuarto dedo dominante (`IV`), el dedo lateral `V` y el par medial sindáctilo `II–III` ligeramente elevado. En las manos prensiles (`freeHand`), la mano continúa hacia delante desde la muñeca sin estrangulamiento (`widthBulge` calibrado) y forma una palma compacta con cinco dedos diferenciados, ahusados, curvados y con garras prensiles, conservando intactas las membranas interdigitales del ornitorrinco y las patas plantígradas de la rata y el pangolín.
- **Ornitorrinco (1012):** `Head.c` eleva la bóveda craneal monotrema (`skullR.y *= 0.88f` sin recorte doble posterior) manteniendo el espesor del pico independiente (`hy`), reordena las seis estaciones craneales para separar el pico aplanado de la frente progresiva y la bóveda redondeada, y asienta los ojos sobre la pendiente orbital detrás del pico.
- **Jirafa (1011):** `HeadModule.h`/`HeadModule.c` publican las ranuras dorsales mediales `HEAD_SLOT_CROWN_LEFT` (22) y `HEAD_SLOT_CROWN_RIGHT` (23) sobre la bóveda parietal sin renumerar las ranuras existentes; `Ornament.c` suaviza la base del osicono y lo enraíza en el cráneo; `Head.c` desplaza lateral y ventralmente los pabellones equinos sólo cuando `earOutward > 0.42`, sin alterar caballo, asno ni canguro.

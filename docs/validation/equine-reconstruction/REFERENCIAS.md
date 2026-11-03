# Comparación previa a la reconstrucción

## Tipo y fuentes

Objetivo: caballo adulto de silla, tipo warmblood ordinario, no un caballo de
tiro ni un árabe de perfil cóncavo. El asno comparte mecanismos, no proporciones.
No se dispone de escaneos ni de medidas veterinarias individuales: las ventanas
de prueba son controles de diseño amplios, no intervalos biológicos certificados.

- [Missouri, Wayne Loch, G2837](https://extension.missouri.edu/publications/g2837):
  equilibrio, musculatura larga y continua, antebrazo y gaskin sustanciales,
  alineación de carpo/cannon/menudillo, talón profundo y pared/pastern alineados.
  La página directa respondió 403; se consultó el texto indexado y el enlace
  [PDF institucional](https://extension.missouri.edu/media/wysiwyg/Extensiondata/Pub/pdf/agguides/ansci/g02837.pdf).
- [Minnesota, Conformation of the horse](https://extension.umn.edu/agriculture/animals-and-livestock/horse/conformation-of-the-horse):
  longitud corporal aproximadamente igual a altura a la cruz; cuello alrededor
  de 1,5 cabezas; hombro y cuartilla de 40–55 grados como guía de conformación,
  no valores universales de todas las razas. Consulta por texto indexado (403
  directo). No confundir longitud axial entre centros con longitud externa.
- [ADW, Clement, 2015](https://animaldiversity.org/accounts/Equus_caballus/):
  tronco profundo y musculado, cabeza alargada, casco oval monodáctilo, pelo corto,
  crin dorsal larga y caída en domésticos. Potros precoces, no adultos a escala.
- [ADW, Pagan, 2022](https://animaldiversity.org/accounts/Equus_asinus/):
  cascos pequeños, fenotipo y pigmentación propios del asno; rango de talla no
  debe convertirse en una proporción exacta de cráneo.
- Los dos capítulos Elsevier indicados en la tarea devolvieron contenido vacío.
  No se atribuyen a ellos mediciones ni hallazgos que no se pudieron consultar.

## Fotografías consultadas y licencias

Fotografías de Tanya Dewey en ADW, **CC BY-NC-SA 3.0**, atribución y enlaces:

- [horse4, warmblood polaco, 2004](https://animaldiversity.org/collections/contributors/tanya_dewey/horse4/):
  referencia principal de volumen, pecho/escápula y tres cuartos.
- [horses3](https://animaldiversity.org/collections/contributors/tanya_dewey/horses3/)
  y [thoroughbreds](https://animaldiversity.org/collections/contributors/tanya_dewey/thoroughbreds/):
  vistas laterales auxiliares; razas distintas, sólo relaciones estructurales.
- [Ecaballus11](https://animaldiversity.org/collections/contributors/tanya_dewey/Ecaballus11/)
  y [paints2](https://animaldiversity.org/collections/contributors/tanya_dewey/paints2/):
  frente, implantación de orejas, órbitas laterales, estrechamiento nasal y labios.
- [sapph1](https://animaldiversity.org/collections/contributors/tanya_dewey/sapph1/):
  detalle lateral de órbita, masetero y mandíbula; **yegua árabe**, no se copia
  su perfil cóncavo en el warmblood.
- Asno adulto: [Patricia Jones, Toledo](https://animaldiversity.org/collections/contributors/tricia_jones/donkey_toledo-pic00018/),
  CC BY-NC-SA 3.0; distinguir de la fotografía de potro «burro» de Dewey.

Las fotografías descargadas para inspección permanecen separadas en
`/tmp/opencode/equine-refs/`; no adquieren la licencia del código. Cualquier hoja
que las reproduzca debe identificar autor, enlace y licencia propia NC-SA.
Las imágenes disponibles no son un juego ortográfico calibrado del mismo animal.
La vista posterior requiere comprobación adicional: no inferir de una vista
frontal una validación fotográfica de grupa y corvejones.

## Diagnóstico de la base (antes de editar geometría)

| Región y landmarks | Fallo visible y causa | Corrección estructural prevista |
|---|---|---|
| Cruz, escápula, punto de hombro, codo | raíz redonda externa; soporte omitido por SDF; radio cae enseguida | unión muscular tipada, origen enterrado, sección aplanada/oblicua y perfil prolongado |
| Grupa, cadera, muslo, babilla, gaskin, corvejón | bola sobre pelvis y zigzag de tubos | estaciones musculares independientes de articulaciones y reducción de desplazamientos |
| Antebrazo, carpo, cannon, menudillo, cuartilla | sólo cuatro centros; desaparece músculo y hay agujas | transiciones de sección y landmarks distales; conservar apoyo y rig |
| Testuz, frente, órbita, puente, hocico | cara casi cilíndrica, cráneo pequeño, casquete plano | mayor frente/masetero, cintura nasal y labios redondeados; perfil precomputado |
| Ganacha y garganta | cabeza/cuello parecen tubo continuo | separar contorno ventral mandibular del collar occipital |
| Cresta cervical, cruz, costillas, vientre, flanco, lomo, grupa | techo y barril uniformes, pelvis única | contornos semánticos y estaciones visuales locales, sin alterar bóvidos |
| Menudillo, corona, pared, talón, punta | plataforma rectangular larga y baja | cápsula oval acotada, corona estrecha, cuartilla oblicua; conservar material y suela |
| Crin, flequillo, maslo y cola | faltan grupos largos de pelo | cobertura regional separada de anatomía, no ornamento sólido |

Base de comparación: commit `1f2877c` y capturas anteriores en
`../equine-anatomy/`, que no se sobrescriben. En la validación final el caballo
adulto pasa compute y fragmento con `6.85453415e-07 m`, y el asno con
`7.15255737e-07 m`; no se subieron tolerancias.

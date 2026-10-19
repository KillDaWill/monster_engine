# Operar el editor mediante IA

La interfaz pública está en `http://127.0.0.1:8765/api/agent`. Iniciar con
`make creature-editor`. No es necesario controlar el navegador ni modificar sus
variables privadas: la UI sigue el mismo estado mediante revisiones.

1. Consultar `agent.py capabilities`, `schema` y `list`. El esquema procede de los
   tipos C e incluye capacidades y nombres de enumerados en orden numérico.
2. Abrir una criatura con `agent.py open builtin:3` o una clave de archivo de la
   biblioteca. Leer `state` antes de editar.
3. Aplicar cambios transaccionales con `set` o `apply` (array de `{path,value}`).
   Las rutas son JSON Pointer y deben existir. Reemplazar arrays completos exige
   actualizar sus conteos y los módulos del plan corporal en la misma transacción.
4. Usar `view` para seleccionar etapa, parte y cámara, `inspect` para consultar
   nodos/materiales y `render --output ruta.png` para revisar la geometría.
   Este PNG es diagnóstico; la demo OpenGL es la referencia de pelaje y animación.
5. Guardar con `save nombre.creature.json`. Las sustituciones deliberadas de una
   receta incorporada conservan su ID con `--catalog-override`; para actualizar un
   archivo existente añadir `--overwrite`.

Cliente: `python3 tools/creature_editor/agent.py --help`. Todas las escrituras HTTP
requieren `expected_revision`; un 409 significa que hay que leer el estado actual
y resolver el cambio concurrente, sin sobrescribirlo silenciosamente. Los errores
de validación son 422 y conservan el documento y la revisión anterior.

`undo` y `redo` operan sobre el historial compartido. La revisión sólo vive durante
la sesión del servidor; los archivos y `recipes/catalog.txt` son la persistencia.
No iniciar dos servidores sobre el mismo catálogo durante una edición concurrente.

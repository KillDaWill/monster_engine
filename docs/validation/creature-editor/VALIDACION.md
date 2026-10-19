# Validación del editor básico — 19 de octubre de 2026

- `make -B -j4 test demos`: recompilación forzada, todas las suites y demos
  completadas con éxito. Sin advertencias nuevas del compilador.
- `make editor-test`: cinco pruebas de integración completadas con éxito;
  incluye mallas para las cuatro recetas del motor en sus cuatro etapas.
- Exportación/importación exacta de todos los campos activos y conservación de
  la huella anatómica de las cuatro etapas; construcción de Monster y rig desde JSON.
- Prueba de lógica JavaScript ejecutada con Node: añadir seis patas, editar un
  par simétrico sin alterar los demás, simetría de anclajes, borrar y restaurar
  extremidades, añadir/eliminar ornamentos y añadir una segunda cola. Cada
  documento resultante fue aceptado por el lector C del motor.
- Comprobación sintáctica de ambos módulos JavaScript y compilación de los cuatro
  scripts Python. `git diff --check` sin incidencias.
- `render_preview.py`: lámina `geometria.png` revisada a partir de los buffers
  reales del puente. Se conserva el informe de tiempos en `geometria.json`.

La reconstrucción de los seis casos de la lámina tardó entre 0,932 y 1,240 s.
Son mediciones locales de esos casos y de calidad rápida, no una garantía de
latencia para cualquier receta.

No se pudo realizar la revisión interactiva de la interfaz: la herramienta de
navegador no tenía Chrome ni navegador integrado disponibles. Las pruebas de
HTTP y la rasterización diagnóstica no sustituyen esa revisión. Tampoco se ha
validado paridad visual con los shaders de pelo y escamas del visor OpenGL;
la primera versión usa iluminación y pigmentación simplificadas.

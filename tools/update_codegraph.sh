#!/usr/bin/env bash
set -euo pipefail

if ! command -v codegraph >/dev/null 2>&1; then
  echo "CodeGraph no está instalado en PATH."
  exit 1
fi

# Si no existe directorio ni enlace simbólico a .codegraph, inicializar
if [ ! -d .codegraph ] && [ ! -L .codegraph ]; then
  codegraph init -i
  exit 0
fi

# Intentar sincronización incremental; si falla, reindexar
if ! codegraph sync .; then
  echo "[codegraph] Sincronización incremental falló, reindexando..."
  codegraph index . --force
fi

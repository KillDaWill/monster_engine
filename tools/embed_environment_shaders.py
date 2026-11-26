#!/usr/bin/env python3
"""Empaqueta GLSL sin dependencias para el build de los renderizadores."""
import json
import sys
from pathlib import Path

output, *sources = map(Path, sys.argv[1:])
with output.open("w") as out:
    out.write("/* Generado por tools/embed_environment_shaders.py. */\n")
    for source in sources:
        lines = [line for line in source.read_text().splitlines(keepends=True)
                 if not line.lstrip().startswith("//")]
        out.write(f"#define ENV_{source.stem.upper()} \\\n")
        out.write(" \\\n".join(json.dumps(line) for line in lines))
        out.write("\n")

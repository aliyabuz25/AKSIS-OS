#!/usr/bin/env python3
"""Build the exact runtime bootstrap into a C string."""
import json
import pathlib
import sys
source, target = map(pathlib.Path, sys.argv[1:])
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('static const char aw_bootstrap[] =\n' + '\n'.join(json.dumps(line) for line in source.read_text().splitlines(keepends=True)) + ';\n')

#!/usr/bin/env python3
"""Rewrite Application.cpp to load only low-res textures on the web build path."""
import re
from pathlib import Path

app_cpp = Path('src/Application.cpp')
text = app_cpp.read_text()

# Replace resource/textures/ with resource/textures_low/ for all direct high-res paths.
text = text.replace('resource/textures/', 'resource/textures_low/')

# Add _Low suffix before .dds for paths in resource/textures_low/ that don't already have it.
def add_low_suffix(match: re.Match) -> str:
    prefix = match.group(1)
    filename = match.group(2)
    if filename.endswith('_Low'):
        return match.group(0)
    return f'{prefix}{filename}_Low.dds'

text = re.sub(r'(resource/textures_low/)([^/\s]+)\.dds', add_low_suffix, text)

app_cpp.write_text(text)
print('Updated', app_cpp)

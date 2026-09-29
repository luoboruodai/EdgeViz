#!/usr/bin/env python3
"""Static guard for EdgeViz's persisted parameter IDs and UI grouping."""
import re
from pathlib import Path

source = Path(__file__).resolve().parents[1].joinpath('src/EdgeVizOutline.cpp').read_text()
enum = 'DISK_COLOR' + source.split('enum {\n  DISK_COLOR', 1)[1].split('\n};', 1)[0]
ids = {name: int(value) for name, value in re.findall(r'\b(DISK_\w+)\s*=\s*(\d+)', enum)}
assert len(ids) == 39, f'expected 39 persisted IDs, got {len(ids)}'
assert set(ids.values()) == set(range(1, 40)), 'duplicate or missing serialized ID'
assert ids['DISK_TARGET'] == 26
assert ids['DISK_SHOW_TEXT'] == 13
assert ids['DISK_SHOW_MOTION'] == 15
assert ids['DISK_TOPIC_PIXEL'] == 35 and ids['DISK_PIXEL_END'] == 36
assert ids['DISK_HANDLE_LENGTH'] == 37 and ids['DISK_POINT_STYLE'] == 38 and ids['DISK_CUSTOM_POINT_LAYER'] == 39

setup = source.split('ParamsSetup(', 1)[1].split('static const char *kParamNames', 1)[0]
registered = re.findall(r'PF_(?:ADD_\w+|END_TOPIC)\([^;]*?\b(DISK_\w+)\s*\);', setup, re.S)
assert len(registered) == 39, f'expected 39 controls, got {len(registered)}'
assert set(registered) == set(ids), 'registered controls differ from serialized IDs'
assert registered[0] == 'DISK_TARGET', 'scope should be first'
assert registered[1] == 'DISK_TOPIC_LANG', 'language should be near the top'
assert registered[4] == 'DISK_TOPIC_STRUCT', 'structure should follow language'
assert registered[-1] == 'DISK_PIXEL_END'
stack = []
for name in registered:
    if name.startswith('DISK_TOPIC_'):
        stack.append(name)
    elif name.endswith('_END'):
        assert stack, f'unpaired group end {name}'
        stack.pop()
assert not stack, f'unclosed groups: {stack}'
assert 'out_data->num_params = PARAM_COUNT;' in setup
print('PASS: 40 runtime params, 39 unique persisted IDs, balanced reordered groups')

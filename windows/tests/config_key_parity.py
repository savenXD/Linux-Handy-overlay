#!/usr/bin/env python3
"""Audit Windows defaults against the Windows parser and checked-in fixture."""
import ast
import configparser
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'src/config.cpp').read_text()
block = source[source.index('const char* const kDefaultIni ='):source.index('// section ->')]
ini = ''.join(ast.literal_eval(item) for item in re.findall(r'"(?:[^"\\]|\\.)*"', block))
assert ini == (root / 'tests/fixtures/config_defaults.ini').read_text(), 'Windows defaults differ from fixture'
fixture = configparser.ConfigParser(interpolation=None)
fixture.read_string(ini)
keys = {(section, key) for section in fixture.sections() for key in fixture[section]}
consumed = set(re.findall(r'(?:GetInt|GetStr|GetDouble|GetBool|Find)\(ini,\s*"([^"]+)",\s*"([^"]+)"', source))
assert keys <= consumed, f'Unused Windows defaults: {keys - consumed}'
assert {('General', 'width'), ('General', 'height'), ('Hotkeys', 'ghost_key'), ('Snap', 'snap_scope')} <= keys
print(f'PASS: {len(keys)} Windows default keys are consumed; generated INI matches fixture')

#!/usr/bin/env python3
"""Read-only audit against the Linux parser; catches omissions as its keys change."""
import ast
import configparser
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
linux = (root / 'src/config.c').read_text()
windows = (root / 'windows/src/config.cpp').read_text()
linux_keys = set(re.findall(r'g_key_file_get_\w+\(kf,\s*"([^"]+)",\s*"([^"]+)"', linux))
windows_keys = set(re.findall(r'(?:GetInt|GetStr|GetDouble|GetBool|Find)\(ini,\s*"([^"]+)",\s*"([^"]+)"', windows))
fixture = configparser.ConfigParser(interpolation=None)
fixture.read(root / 'shared/fixtures/config_defaults.ini')
fixture_keys = {(section, key) for section in fixture.sections() for key in fixture[section]}
# The two legacy fallback locations are read by both parsers but do not need duplicate defaults.
legacy = {('Snap', 'snap_key'), ('GhostWriter', 'ghost_writer_key')}
missing = linux_keys - windows_keys
unexpected = windows_keys - linux_keys
implicit = {('Browser', 'chatgpt_allowlist'), ('Hotkeys', 'shield_clipboard_keys')}
missing_defaults = linux_keys - legacy - implicit - fixture_keys
if not linux_keys or missing or unexpected or missing_defaults:
    print(f'FAIL: missing Windows keys={sorted(missing)}, extra keys={sorted(unexpected)}, missing fixture defaults={sorted(missing_defaults)}')
    sys.exit(1)
def literal_block(source, pattern):
    block = re.search(pattern, source, re.S)
    if not block:
        raise AssertionError(f"C string block not found: {pattern}")
    return ''.join(ast.literal_eval(item) for item in re.findall(r'"(?:[^"\\]|\\.)*"', block[1]))

c_string = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
linux_ini = literal_block(linux, r'const char \*default_ini\s*=\s*' + c_string + ';')
windows_ini = literal_block(windows, r'kDefaultIni\s*=\s*' + c_string + ';')
assert linux_ini == windows_ini == (root / 'shared/fixtures/config_defaults.ini').read_text(), 'Generated default INI differs'
linux_allowlist = literal_block(linux, r'g_strlcpy\(cfg->chatgpt_allowlist,\s*(.*?),\s*sizeof')
windows_header = (root / 'windows/src/config.h').read_text()
windows_allowlist = literal_block(windows_header, r'chatgptAllowlist\s*=\s*(.*?);')
assert linux_allowlist == windows_allowlist, 'Implicit ChatGPT allowlist differs'
assert 'cfg->shield_clipboard_keys = true;' in linux and 'bool shieldClipboardKeys = true;' in windows_header
print(f'PASS: {len(linux_keys)} Linux section/key pairs implemented in Windows; default INI and implicit defaults match Linux')

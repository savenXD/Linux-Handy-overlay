"""Prepare a browser-only mock host in the ignored build directory; no WebView2 claim."""
from pathlib import Path
import shutil
root = Path(__file__).resolve().parents[1]
output = root / 'build-mingw' / 'ui-preview'
output.mkdir(parents=True, exist_ok=True)
for source in (root / 'ui').iterdir():
    if source.is_file():
        shutil.copy2(source, output / source.name)
shutil.copy2(root / 'tests/ui_preview_host.js', output / 'ui_preview_host.js')
p = output / 'index.html'
p.write_text(p.read_text().replace('<script src="cronq.js" defer>', '<script src="ui_preview_host.js" defer></script><script src="cronq.js" defer>'))
print(output)

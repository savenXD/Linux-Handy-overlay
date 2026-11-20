#!/usr/bin/env python3
"""Exercise restart policy with a disposable executable, never the user's daemon."""
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    work=Path(tmp);fake=work/'child'
    fake.write_text('''#!/usr/bin/env bash
printf '%s\\n' "$*" >> "$XDG_DATA_HOME/args"
if [ ! -f "$XDG_DATA_HOME/once" ];then touch "$XDG_DATA_HOME/once";exit 9;fi
exit 0
''');fake.chmod(0o755)
    env=dict(os.environ,CRONQ_BIN=str(fake),XDG_DATA_HOME=str(work))
    subprocess.run(['bash',str(root/'scripts/cronq-supervisor'),'--show'],env=env,check=True,timeout=10)
    assert (work/'args').read_text().splitlines()==['--show','--daemon']
    assert 'Restarting hidden in 2s' in (work/'cronq/crash.log').read_text()
    fake.write_text('#!/usr/bin/env bash\nexit 3\n')
    subprocess.run(['bash',str(root/'scripts/cronq-supervisor'),'--show'],env=env,check=True,timeout=2)
    print('PASS supervisor crash restart is hidden; clean and intentional exits stop supervision')

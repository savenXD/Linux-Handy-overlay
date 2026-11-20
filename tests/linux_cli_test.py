#!/usr/bin/env python3
"""Runs only on the disposable X11/XDG environment created by the test runner."""
import configparser
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time

root=Path(__file__).resolve().parents[1]
binary=root/'bin/cronq'
data=Path(os.environ['XDG_DATA_HOME'])/'cronq'
log=open(data.parent/'daemon.log','w')
p=subprocess.Popen([binary,'--daemon'],stdout=log,stderr=subprocess.STDOUT)
def ipc(command):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5);s.connect(str(data/'ipc.sock'));s.sendall((command+'\n').encode());return s.recv(4096).decode().strip()
def check(name,condition):
    assert condition,name
    print('PASS',name,flush=True)
def state():return dict(x.split('=') for x in ipc('STATUS').split())
def key(keys):subprocess.run(['xdotool','key','--clearmodifiers',keys],check=True);time.sleep(.15)
try:
    for _ in range(100):
        try:ipc('STATUS');break
        except OSError:time.sleep(.1)
    check('version matches both platforms',subprocess.check_output([binary,'--version'],text=True).strip()=='Cronq '+(root/'windows/VERSION').read_text().strip())
    check('daemon starts hidden and normal',state()['VISIBLE']=='FALSE' and state()['MODE']=='NORMAL')
    for command in ('MODE mirror','MODE exclusive','GHOST_WRITE','LOAD https://example.com','GET_TEST_ENTRY','SET_TEST_ENTRY old','CLEAR_TEST_ENTRY','SNAP_WGC','SNAP_WGC_WINDOW','GOD_COPY','GOD_PASTE'):
        check('retired/excluded IPC rejected: '+command,ipc(command)=='UNKNOWN_COMMAND')
    for command in ('APP browser','APP ghost'):
        check('retired app rejected: '+command,ipc(command)=='UNKNOWN_APP')
    for option in ('--ghost-write','--load','--god-copy','--god-paste'):
        check('retired CLI rejected: '+option,subprocess.run([binary,option],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0)
    ipc('SHOW');time.sleep(.2)
    check('show works',state()['VISIBLE']=='TRUE')
    ipc('MODE overlay');check('overlay route active',state()['MODE']=='OVERLAY')
    ipc('HIDE');check('hide preserves typing',state()['MODE']=='OVERLAY')
    key('alt+m');check('hidden mode shortcut passes through',state()['MODE']=='NORMAL')
    ipc('SHOW');ipc('DEFAULT_SIZE');time.sleep(.2)
    geometry=dict(x.split('=') for x in ipc('GET_GEOMETRY').split())
    check('default Home is compact',int(geometry['H'])==280)
    ipc('APP chatgpt');time.sleep(.2);ipc('DEFAULT_SIZE');time.sleep(.2)
    geometry=dict(x.split('=') for x in ipc('GET_GEOMETRY').split())
    check('ChatGPT fits work area',int(geometry['W'])==360 and int(geometry['H'])==640)
    ipc('HOME');ipc('FULLSCREEN');time.sleep(.2)
    geometry=dict(x.split('=') for x in ipc('GET_GEOMETRY').split())
    check('fullscreen exact monitor',geometry==dict(X='0',Y='0',W='1280',H='900'))
    ipc('FULLSCREEN');ipc('SET_OPACITY 0.3');check('opacity setting',ipc('GET_OPACITY')=='0.30')
    key('alt+k');check('Alt+K hides and locks',state()['LOCKED']=='TRUE' and state()['VISIBLE']=='FALSE')
    key('alt+x');check('locked toggle disabled',state()['VISIBLE']=='FALSE')
    key('alt+k+x');check('unlock restores overlay',state()['LOCKED']=='FALSE' and state()['VISIBLE']=='TRUE')
    key('alt+c');key('alt+t');key('alt+Right')
    config=configparser.ConfigParser();config.read(Path(os.environ['XDG_CONFIG_HOME'])/'cronq/config.ini')
    check('theme and dock persisted',config['General']['corner']=='bottom-right' and int(config['Appearance']['mode'])==2)
    ipc('MODE overlay');ipc('KILL_PROMPT');check('kill prompt releases typing',state()['MODE']=='NORMAL')
    key('Escape');check('kill prompt safely cancelled',ipc('KILL_PROMPT_ACTIVE')=='FALSE')
    ipc('HIDE');ipc('KILL_PROMPT');check('hidden prompt armed',ipc('KILL_PROMPT_ACTIVE')=='TRUE')
    time.sleep(5.2);check('hidden prompt expires after five seconds',ipc('KILL_PROMPT_ACTIVE')=='FALSE')
    # Known colored root pixels allow validation that all-black frames are rejected separately.
    subprocess.run(['xsetroot','-solid','#5588aa'],check=True)
    config_path=Path(os.environ['XDG_CONFIG_HOME'])/'cronq/config.ini'
    config.read(config_path);config['Snap']={'snap_save':'true','snap_keep_count':'2','snap_delay_ms':'80'}
    with config_path.open('w') as stream:config.write(stream)
    ipc('RELOAD_CONFIG');ipc('SHOW')
    for n in range(3):
        ipc('SNAP');time.sleep(1.2)
    snaps=sorted((data/'snaps').glob('snap-*.png'))
    check('snap retention keeps latest two',len(snaps)==2)
    for image in snaps:
        raw=image.read_bytes();check('snapshot valid PNG and exact monitor dimensions',raw[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',raw[16:24])==(1280,900))
    clipboard_check=[root/'tests/linux_runtime_test','--clipboard-matches',snaps[-1]]
    check('clipboard pixels exactly match saved screenshot',subprocess.run(clipboard_check).returncode==0)
    previous={f.name:f.read_bytes() for f in snaps}
    subprocess.run(['xsetroot','-solid','black'],check=True)
    ipc('SNAP');time.sleep(.4)
    check('all-black capture preserves previous screenshots',{f.name:f.read_bytes() for f in (data/'snaps').glob('snap-*.png')}==previous)
    check('all-black capture preserves clipboard',subprocess.run(clipboard_check).returncode==0)
    subprocess.run(['xsetroot','-solid','#5588aa'],check=True)
    ipc('SNAP');ipc('HIDE');time.sleep(.4)
    check('hide during screenshot stays hidden',state()['VISIBLE']=='FALSE')
    ipc('SHOW')
    check('snap restores prior overlay visibility',state()['VISIBLE']=='TRUE')
    ipc('RESET');check('reset normal Home',state()['MODE']=='NORMAL' and state()['APP']=='home')
    ipc('KILL');p.wait(timeout=10);check('clean daemon shutdown',p.returncode==0)
    # Crash handler runs only on a new disposable daemon; no kill-switch confirmation is ever used.
    for n in range(4):
        p=subprocess.Popen([binary,'--daemon'],stdout=log,stderr=subprocess.STDOUT)
        for _ in range(100):
            try:ipc('STATUS');break
            except OSError:time.sleep(.05)
        p.send_signal(signal.SIGABRT);p.wait(timeout=10)
    reports=list(data.glob('crash-*.txt'))
    check('native crash reports bounded to three',len(reports)==3)
    check('native crash report has signal and instruction',all('signal=0x6' in f.read_text() and 'instruction=0x' in f.read_text() for f in reports))
finally:
    if p.poll() is None:p.terminate();p.wait(timeout=10)
    log.close()

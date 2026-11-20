#!/usr/bin/env python3
"""Manual, per-user Linux release updater. No background checks or telemetry."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

REPO = 'https://github.com/savenXD/Linux-Handy-overlay/releases/download/'
API = 'https://api.github.com/repos/savenXD/Linux-Handy-overlay/releases/latest'
FILES = {'cronq': 0o755, 'cronq-supervisor': 0o755, 'cronq-update.py': 0o755,
         'cronq.desktop': 0o644, 'cronq.svg': 0o644, 'VERSION': 0o644}
LIMIT = 200 * 1024 * 1024


def version(text):
    if not re.fullmatch(r'v?\d+\.\d+\.\d+', text):
        raise ValueError('Invalid release version')
    return tuple(map(int, text.lstrip('v').split('.')))


def fetch(url, limit=LIMIT):
    if not url.startswith('https://'):
        raise ValueError('HTTPS is required')
    class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            if not newurl.startswith('https://'):
                raise ValueError('Refusing insecure redirect')
            return super().redirect_request(req, fp, code, msg, headers, newurl)
    req = urllib.request.Request(url, headers={'User-Agent': 'cronq-updater', 'Accept': 'application/vnd.github+json, application/octet-stream'})
    with urllib.request.build_opener(HTTPSRedirect()).open(req, timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Download exceeds size limit')
    return data


def select_release(release, current, machine):
    tag = release['tag_name']
    if version(tag) <= version(current):
        return None
    if release.get('prerelease') or release.get('draft'):
        raise ValueError('Latest release is not stable')
    arch = {'x86_64': 'x86_64', 'aarch64': 'aarch64'}.get(machine)
    if not arch:
        raise ValueError('No Linux release for this CPU')
    name = f'cronq-linux-{arch}.tar.gz'
    assets = {a['name']: a['browser_download_url'] for a in release.get('assets', [])}
    expected = REPO + tag + '/'
    for asset in (name, 'SHA256SUMS-linux.txt'):
        if assets.get(asset) != expected + asset:
            raise ValueError(f'Release is missing the official {asset} asset')
    return name, assets[name], assets['SHA256SUMS-linux.txt']


def unpack(data, sums, name, expected_version):
    hashes = [line.split() for line in sums.decode('ascii').splitlines()]
    expected = [h[0] for h in hashes if len(h) == 2 and h[1].lstrip('*') == name]
    if len(expected) != 1 or not re.fullmatch(r'[a-fA-F0-9]{64}', expected[0]) or hashlib.sha256(data).hexdigest() != expected[0].lower():
        raise ValueError('Release SHA256 verification failed')
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive:
            if member.name not in FILES or not member.isfile() or member.name in files or member.size > LIMIT:
                raise ValueError('Unsafe or unexpected archive member')
            files[member.name] = archive.extractfile(member).read()
    if set(files) != set(FILES) or version(files['VERSION'].decode().strip()) != version(expected_version):
        raise ValueError('Incomplete or mismatched release')
    if not files['cronq'].startswith(b'\x7fELF'):
        raise ValueError('Release does not contain a Linux executable')
    return files


def install(files, home, data, config):
    targets = {'cronq': home/'.local/bin/cronq', 'cronq-supervisor': home/'.local/bin/cronq-supervisor',
               'cronq-update.py': data/'cronq/update.py', 'cronq.desktop': data/'applications/cronq.desktop',
               'cronq.svg': data/'icons/hicolor/scalable/apps/cronq.svg', 'VERSION': data/'cronq/VERSION'}
    # Profile, cookies, screenshots, and user settings are deliberately outside this mapping.
    originals = {}
    try:
        for name, target in targets.items():
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink():
                raise ValueError(f'Refusing symlink destination: {target}')
            originals[target] = (target.read_bytes(), target.stat().st_mode & 0o777) if target.exists() else None
            fd, staging = tempfile.mkstemp(prefix='.cronq-update-', dir=target.parent)
            try:
                with os.fdopen(fd, 'wb') as stream:
                    stream.write(files[name]); stream.flush(); os.fsync(stream.fileno())
                os.chmod(staging, FILES[name]); os.replace(staging, target)
            finally:
                if os.path.exists(staging): os.unlink(staging)
        # Refresh an existing autostart entry, never enable autostart as an update side effect.
        autostart = config/'autostart/cronq.desktop'
        if autostart.is_file() and not autostart.is_symlink():
            originals[autostart] = (autostart.read_bytes(), autostart.stat().st_mode & 0o777)
            autostart.write_bytes(files['cronq.desktop'])
    except Exception:
        for target, previous in reversed(list(originals.items())):
            if previous is None: target.unlink(missing_ok=True)
            else: target.write_bytes(previous[0]); target.chmod(previous[1])
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current', required=True)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--yes', action='store_true')
    args = parser.parse_args()
    release = json.loads(fetch(API, 2 * 1024 * 1024))
    selected = select_release(release, args.current, platform.machine())
    if selected is None:
        print(f'Cronq {args.current} is up to date.'); return
    name, asset, sums = selected
    print(f'Cronq {args.current} → {release["tag_name"]}')
    if not args.yes and input('Download, verify and install this update? [y/N] ').strip().lower() not in ('y', 'yes'):
        print('Update cancelled.'); return
    files = unpack(fetch(asset), fetch(sums, 16384), name, release['tag_name'])
    # The existing socket belongs to this user/profile. Stop only that instance after verification.
    subprocess.run([args.binary, '--kill'], check=True, timeout=10)
    home = Path.home()
    install(files, home, Path(os.environ.get('XDG_DATA_HOME', home/'.local/share')),
            Path(os.environ.get('XDG_CONFIG_HOME', home/'.config')))
    print('Update installed. Start cronq to use the new version.')


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, KeyError, EOFError, tarfile.TarError, subprocess.SubprocessError) as exc:
        print(f'Update failed: {exc}', file=sys.stderr); sys.exit(1)

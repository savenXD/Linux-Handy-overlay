#!/usr/bin/env python3
import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('updater',ROOT/'scripts/cronq-update.py')
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)

class UpdateTest(unittest.TestCase):
    def release(self):
        names=('cronq-linux-x86_64.tar.gz','SHA256SUMS-linux.txt')
        return {'tag_name':'v1.4.2','assets':[{'name':n,'browser_download_url':u.REPO+'v1.4.2/'+n} for n in names]}
    def archive(self, extra=None):
        files={n:b'fixture' for n in u.FILES};files.update(cronq=b'\x7fELFfixture',VERSION=b'1.4.2\n')
        stream=io.BytesIO()
        with tarfile.open(fileobj=stream,mode='w:gz') as archive:
            for name,data in files.items():
                info=tarfile.TarInfo(name);info.size=len(data);archive.addfile(info,io.BytesIO(data))
            if extra: archive.addfile(extra)
        data=stream.getvalue();sums=(hashlib.sha256(data).hexdigest()+'  cronq-linux-x86_64.tar.gz\n').encode()
        return data,sums
    def test_versions(self):
        self.assertGreater(u.version('1.10.0'),u.version('v1.9.9'))
        for value in ('v1.2','../1.2.3','1.2.3-beta','-1.2.3'):
            with self.assertRaises(ValueError):u.version(value)
    def test_asset_origin_and_arch(self):
        r=self.release();self.assertIsNotNone(u.select_release(r,'1.4.1','x86_64'))
        self.assertIsNone(u.select_release(r,'1.4.2','x86_64'))
        with self.assertRaises(ValueError):u.select_release(r,'1.4.1','riscv64')
        r['assets'][0]['browser_download_url']='https://example.com/payload'
        with self.assertRaises(ValueError):u.select_release(r,'1.4.1','x86_64')
    def test_integrity(self):
        data,sums=self.archive();files=u.unpack(data,sums,'cronq-linux-x86_64.tar.gz','v1.4.2')
        self.assertEqual(set(files),set(u.FILES))
        with self.assertRaises(ValueError):u.unpack(data+b'bad',sums,'cronq-linux-x86_64.tar.gz','v1.4.2')
        with self.assertRaises(ValueError):u.unpack(data,sums,'cronq-linux-x86_64.tar.gz','v1.4.3')
    def test_archive_traversal_and_links(self):
        for name,kind in (('../escape',tarfile.REGTYPE),('/tmp/escape',tarfile.REGTYPE),('link',tarfile.SYMTYPE)):
            info=tarfile.TarInfo(name);info.type=kind;info.linkname='/etc/passwd'
            data,sums=self.archive(info)
            with self.assertRaises(ValueError):u.unpack(data,sums,'cronq-linux-x86_64.tar.gz','1.4.2')
    def test_install_preserves_profile_and_rolls_back(self):
        data,sums=self.archive();files=u.unpack(data,sums,'cronq-linux-x86_64.tar.gz','1.4.2')
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp);profile=home/'data/cronq/data/cookies.sqlite';profile.parent.mkdir(parents=True);profile.write_text('saved login')
            config=home/'config/cronq/config.ini';config.parent.mkdir(parents=True);config.write_text('custom')
            u.install(files,home,home/'data',home/'config')
            self.assertEqual(profile.read_text(),'saved login');self.assertEqual(config.read_text(),'custom')
            binary=home/'.local/bin/cronq';binary.write_bytes(b'old')
            supervisor=home/'.local/bin/cronq-supervisor';supervisor.unlink();supervisor.symlink_to('/dev/null')
            with self.assertRaises(ValueError):u.install(files,home,home/'data',home/'config')
            self.assertEqual(binary.read_bytes(),b'old')
    def test_checked_in_defaults_and_version_match(self):
        import ast,re
        src=(ROOT/'src/config.c').read_text();block=src.split('const char *default_ini =',1)[1].split(';',1)[0]
        ini=''.join(ast.literal_eval(t) for t in re.findall(r'"(?:[^"\\]|\\.)*"',block))
        self.assertEqual(ini,(ROOT/'shared/fixtures/config_defaults.ini').read_text())
        self.assertEqual(ini,(ROOT/'windows/tests/fixtures/config_defaults.ini').read_text())
        self.assertEqual((ROOT/'VERSION').read_text(),(ROOT/'windows/VERSION').read_text())

if __name__=='__main__':unittest.main(verbosity=2)

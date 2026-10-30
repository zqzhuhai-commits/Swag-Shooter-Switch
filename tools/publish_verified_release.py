"""Publish an explicitly verified package; never rebuild the tested binary.

Binary chunks are unattached Git blobs, not files in the source tree/history.
The committed manifest pins their Git/SHA256 hashes and the tested NSP hash.
Requires a GitHub Actions token with contents:write for this repository only.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

REPO = 'zqzhuhai-commits/Swag-Shooter-Switch'
API = 'https://api.github.com/repos/' + REPO
TAG = 'v0.14.38'
NSP_SHA = '64ff7db43052ea7344fcda1e4e6ae753d9793c5dbd9a4274f606a072c6822311'
ZIP_SHA = 'f5b675228773e144c98794589ed87aaecc82665cf6fdc3aaa29468803ee4f67f'
SOURCE = 'c1c093312583a8eb615b8b3324e686c65295f84f'

def request(path, method='GET', payload=None, raw=None, content_type=None):
    url = path if path.startswith('https://') else API + path
    assert url.startswith((API + '/', 'https://uploads.github.com/repos/' + REPO + '/'))
    headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
               'Accept': 'application/vnd.github+json', 'User-Agent': 'verified-release-publisher',
               'X-GitHub-Api-Version': '2022-11-28'}
    data = json.dumps(payload).encode() if payload is not None else raw
    if payload is not None:
        headers['Content-Type'] = 'application/json'
    elif content_type:
        headers['Content-Type'] = content_type
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers, method=method), timeout=300) as response:
        return json.load(response)

def digest_file(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def main():
    manifest = json.loads(Path(sys.argv[1]).read_text())
    assert manifest['tag'] == TAG and manifest['source_commit'] == SOURCE
    assert manifest['zip_sha256'] == ZIP_SHA and manifest['nsp_sha256'] == NSP_SHA
    folder = Path('release-output'); folder.mkdir(exist_ok=True)
    archive = folder / 'Swag-Shooter-0.14.38.zip'
    with archive.open('wb') as stream:
        for index, chunk in enumerate(manifest['chunks']):
            assert chunk['index'] == index
            blob = request('/git/blobs/' + chunk['git_sha'])
            assert blob['encoding'] == 'base64' and blob['sha'] == chunk['git_sha']
            content = base64.b64decode(blob['content'])
            assert len(content) == chunk['bytes']
            assert hashlib.sha256(content).hexdigest() == chunk['sha256']
            assert hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest() == chunk['git_sha']
            stream.write(content)
            print('Verified chunk', index + 1, '/', len(manifest['chunks']), flush=True)
    assert digest_file(archive) == ZIP_SHA
    with zipfile.ZipFile(archive) as package:
        nsps = [n for n in package.namelist() if n.endswith('.nsp')]
        assert len(nsps) == 1
        h = hashlib.sha256()
        with package.open(nsps[0]) as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                h.update(block)
        assert h.hexdigest() == NSP_SHA
        for name in ['INSTALL.zh-CN.txt', 'verification-report.json', 'Swag-Shooter-0.14.38.nsp.sha256']:
            (folder / name).write_bytes(package.read(name))
    (folder / (archive.name + '.sha256')).write_text(ZIP_SHA + '  ' + archive.name + '\n')
    assets = [archive] + [folder / n for n in ['INSTALL.zh-CN.txt', 'verification-report.json',
               'Swag-Shooter-0.14.38.nsp.sha256', 'Swag-Shooter-0.14.38.zip.sha256']]
    try:
        release = request('/releases/tags/' + TAG)
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        release = request('/releases', 'POST', {'tag_name': TAG, 'target_commitish': SOURCE,
            'name': '绝地精英 0.14.38 / Xiao Fan', 'body': Path('RELEASE-0.14.38.md').read_text(encoding='utf-8'),
            'draft': True, 'prerelease': False})
    existing = {a['name']: a for a in release['assets']}
    if not release['draft']:
        assert all(p.name in existing and existing[p.name].get('digest') == 'sha256:' + digest_file(p) for p in assets)
        print('Identical release already published:', release['html_url'])
        return
    for path in assets:
        expected = 'sha256:' + digest_file(path)
        if path.name in existing:
            assert existing[path.name].get('digest') == expected, 'Conflicting draft attachment'
            continue
        upload = release['upload_url'].split('{')[0] + '?name=' + urllib.parse.quote(path.name)
        asset = request(upload, 'POST', raw=path.read_bytes(), content_type='application/octet-stream')
        assert asset['state'] == 'uploaded' and asset['size'] == path.stat().st_size
        assert asset.get('digest') == expected
        print('Uploaded:', asset['name'], flush=True)
    check = request('/releases/' + str(release['id']))
    by_name = {a['name']: a for a in check['assets']}
    assert all(p.name in by_name and by_name[p.name].get('digest') == 'sha256:' + digest_file(p) for p in assets)
    published = request('/releases/' + str(release['id']), 'PATCH', {'draft': False, 'prerelease': False, 'make_latest': 'true'})
    assert not published['draft'] and not published['prerelease']
    print('PUBLISHED:', published['html_url'], flush=True)

if __name__ == '__main__':
    main()

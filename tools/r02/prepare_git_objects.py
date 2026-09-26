#!/usr/bin/env python3
"""Publish only immutable prepared source objects. Never writes branches or commits."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

ALLOWED = ('libil2cpp/vm/AssemblyShadow.cpp', 'libil2cpp/vm/AssemblyShadowTypeResolver.cpp')

def main() -> None:
    repo = os.environ['GITHUB_REPOSITORY']
    if repo != 'night-outlook/il2cpp_plus': raise ValueError('Unexpected repository')
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if head != os.environ['GITHUB_SHA']: raise ValueError('Checkout/run identity mismatch')
    changed = subprocess.check_output(['git', 'diff', '--name-only'], text=True).splitlines()
    if set(changed) - set(ALLOWED): raise ValueError('Unrelated source modifications')
    rows = []
    for path in ALLOWED:
        raw = Path(path).read_bytes()
        rows.append({'path': path, 'sha256': hashlib.sha256(raw).hexdigest(),
            'gitBlob': hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()})
    receipt = {'kind': 'R02PreparedSourceObjects', 'sourceHead': head, 'files': rows,
        'status': 'CommittedSourcesVerified' if not changed else 'PreparedNotCommitted',
        'runtimeAcceptance': False, 'refModified': False}
    if changed:
        token = os.environ['GH_TOKEN']
        def post(endpoint, payload):
            request = urllib.request.Request('https://api.github.com/repos/'+repo+'/'+endpoint,
                data=json.dumps(payload).encode(), method='POST', headers={
                'Authorization': 'Bearer '+token, 'Accept': 'application/vnd.github+json',
                'X-GitHub-Api-Version': '2022-11-28', 'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response)
        entries = []
        for row in rows:
            created = post('git/blobs', {'content': Path(row['path']).read_text(), 'encoding': 'utf-8'})
            if created['sha'] != row['gitBlob']: raise ValueError('Created Git blob differs')
            entries.append({'path': row['path'], 'mode': '100644', 'type': 'blob', 'sha': created['sha']})
        base = subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], text=True).strip()
        tree = post('git/trees', {'base_tree': base, 'tree': entries})
        receipt['preparedTree'] = tree['sha']
    Path(os.environ['R02_RESULTS'], 'prepared-source-objects.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('R02_PREPARED_SOURCE_OBJECTS='+json.dumps(receipt, separators=(',', ':')))

if __name__ == '__main__': main()

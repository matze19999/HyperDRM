"""Describe the exact built IPK for Homebrew Channel."""
import hashlib, json
from pathlib import Path
from sync_version import synchronize
synchronize(check=True)
ROOT=Path(__file__).resolve().parent
BASE='https://github.com/matze19999/HyperDRM'
RAW='https://raw.githubusercontent.com/matze19999/HyperDRM/'
app=json.loads((ROOT/'app/appinfo.json').read_text(encoding='utf-8'))
ipk=ROOT/('HyperDRM_'+app['version']+'_arm.ipk')
data=ipk.read_bytes()
manifest={key:app[key] for key in ('id','version','type','title','appDescription')}
manifest.update(iconUri=RAW+'main/app/icon.png',sourceUrl=BASE,rootRequired=True,
                ipkUrl=f"{RAW}v{app['version']}/{ipk.name}",ipkHash={'sha256':hashlib.sha256(data).hexdigest()},ipkSize=len(data))
(ROOT/'HyperDRM.manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print('Manifest:',ipk.name,len(data),'bytes',manifest['ipkHash']['sha256'])

#!/usr/bin/env python3
"""Validate multi-board installer catalog (host-only, stdlib)."""
import argparse, json, pathlib, re, sys
from urllib.parse import urlsplit
ROOT=pathlib.Path(__file__).resolve().parents[2]
DEFAULT=ROOT/'hardware'/'boards.json'
STAGES={'concept','planned','bringup','driver-verified','released'}
FORMS={'headless','touchscreen','development-board'}
IDENT=re.compile(r'^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$')
class CatalogError(ValueError):pass
def validate(data):
    if not isinstance(data,dict) or data.get('schema_version')!=1:
        raise CatalogError('expected schema_version=1')
    boards=data.get('boards')
    if not isinstance(boards,list) or not boards:
        raise CatalogError('boards must be a nonempty array')
    used=set()
    for b in boards:
        if not isinstance(b,dict):raise CatalogError('board must be an object')
        key=b.get('id')
        if not isinstance(key,str) or not IDENT.fullmatch(key) or key in used:
            raise CatalogError('invalid or duplicate board id')
        used.add(key)
        for k in ('name','vendor','chip_family','stage','form_factor','usb_host','notes'):
            if not isinstance(b.get(k),str) or not b[k]:
                raise CatalogError(key+': missing '+k)
        if b['chip_family']!='ESP32-P4':
            raise CatalogError(key+': unsupported chip (extend validator deliberately)')
        if b['stage'] not in STAGES or b['form_factor'] not in FORMS:
            raise CatalogError(key+': invalid stage/form-factor')
        display=b.get('display')
        if display is not None and (not isinstance(display,dict)
                or type(display.get('width')) is not int
                or type(display.get('height')) is not int
                or min(display['width'],display['height'])<1
                or type(display.get('touch')) is not bool):
            raise CatalogError(key+': invalid display')
        if b['form_factor']=='touchscreen' and (display is None or not display['touch']):
            raise CatalogError(key+': touch display required')
        if b['form_factor']=='headless' and display is not None:
            raise CatalogError(key+': headless must have no display')
        app=b.get('app_path')
        if app is not None and (not isinstance(app,str)
                or not re.fullmatch(r'apps/[a-z0-9-]+',app)):
            raise CatalogError(key+': invalid app_path')
        flash=b.get('flash')
        if not isinstance(flash,dict) or type(flash.get('enabled')) is not bool:
            raise CatalogError(key+': flash.enabled must be explicit')
        url=flash.get('manifest_url')
        if flash['enabled']:
            if b['stage']!='released' or not app:
                raise CatalogError(key+': unpublished target cannot be flashed')
            if not isinstance(url,str):raise CatalogError(key+': missing manifest URL')
            parsed=urlsplit(url)
            if parsed.scheme!='https' or not parsed.netloc or not parsed.path.endswith('.json') or parsed.username or parsed.password:
                raise CatalogError(key+': insecure/invalid manifest URL')
        elif url is not None:raise CatalogError(key+': unpublished target has manifest URL')
    return True
def load(path=DEFAULT):
    try:data=json.loads(pathlib.Path(path).read_text(encoding='utf-8'))
    except (OSError,json.JSONDecodeError) as e:raise CatalogError(str(e)) from e
    validate(data)
    return data
def public_catalog(data):
    validate(data)
    return {'schema_version':1,'boards':[
        {k:b[k] for k in ('id','name','vendor','chip_family','stage','form_factor','display')}
        | {'install_enabled':b['flash']['enabled'],
           'manifest_url':b['flash']['manifest_url']}
        for b in data['boards']]}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=pathlib.Path,default=DEFAULT)
    p.add_argument('--public',action='store_true')
    a=p.parse_args()
    try:cat=load(a.catalog)
    except CatalogError as e:sys.exit('catalog invalid: '+str(e))
    if a.public:print(json.dumps(public_catalog(cat),indent=2))
    else:print(f"catalog valid: {len(cat['boards'])} boards; "+
               f"{sum(b['flash']['enabled'] for b in cat['boards'])} installable")
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Read-only REA-style *source* audit, not REA/Ghidra binary analysis.
Usage: python3 tools/hardware/rea_graphics_source_audit.py --bsp BSP.c
       --tab5-ui apps/orcsdr-tab5/ui --out-dir evidence
Outputs evidence JSON and a logfile in the requested directory.
"""
import argparse, datetime, hashlib, json, pathlib
BSP=('bsp_display_start','bsp_display_start_with_config',
     'bsp_display_new_with_handles','bsp_display_rotate','bsp_touch_new',
     'bsp_display_lock','bsp_display_unlock','bsp_usb_host_start',
     'usb_host_install','esp_lv_adapter_start')
TAB=('M5.Display.','M5.Touch.','M5.Imu.','setRotation(',
     'M5Canvas','g_frame_buffers','drawString(','pushImage(')
def audit(path,tokens):
    raw=path.read_bytes()
    lines=raw.decode('utf-8','replace').splitlines()
    return {'path':str(path.resolve()),'sha256':hashlib.sha256(raw).hexdigest(),
            'bytes':len(raw),'hits':{
              tok:{'count':sum(tok in s for s in lines),
                   'first_sites':[{'line':i,'text':s.strip()[:140]}
                                  for i,s in enumerate(lines,1) if tok in s][:12]}
              for tok in tokens}}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bsp',required=True,type=pathlib.Path)
    p.add_argument('--tab5-ui',required=True,type=pathlib.Path)
    p.add_argument('--out-dir',required=True,type=pathlib.Path)
    a=p.parse_args()
    if not a.bsp.is_file() or not a.tab5_ui.is_dir():
        p.error('BSP C source file and Tab5 UI directory required')
    sources=sorted(x for x in a.tab5_ui.rglob('*') if x.is_file()
                   and x.suffix in ('.cpp','.hpp'))
    if not sources:p.error('no Tab5 sources')
    rep={'schema':1,'type':'source-callsites-NOT-REA-binary',
         'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
         'waveshare':audit(a.bsp,BSP),'tab5':[audit(x,TAB) for x in sources],
         'warning':'Source matches do not establish runtime performance.'}
    a.out_dir.mkdir(parents=True,exist_ok=True)
    (a.out_dir/'rea-graphics-source.json').write_text(json.dumps(rep,indent=2)+'\n')
    lines=['Graphics source audit (no REA binary stage)',rep['utc'],
           'Waveshare SHA256 '+rep['waveshare']['sha256'],
           'Tab5 source files '+str(len(sources))]
    lines += [k+': '+str(v['count']) for k,v in rep['waveshare']['hits'].items()]
    lines.append(rep['warning'])
    (a.out_dir/'rea-graphics-source.log').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))
if __name__=='__main__':main()

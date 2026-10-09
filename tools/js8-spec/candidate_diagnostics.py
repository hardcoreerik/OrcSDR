#!/usr/bin/env python3
"""Spec-only diagnostic: match saved pre-FEC tone candidates to reference index metadata.
No FEC/CRC, no decoder code. Usage: python candidate_diagnostics.py EXTRACTED_CORE --output results.json
"""
import argparse,hashlib,json
from pathlib import Path
SYNC='4256130'
def main():
 p=argparse.ArgumentParser();p.add_argument('core',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 root=a.core
 raw=json.loads((root/'orcsdr/docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json').read_text())
 f=root/'dataset/2026-10-08/captures/sample-40m-180s-002/fixture.json';fixture=json.loads(f.read_text())
 h=hashlib.sha256((f.parent/'audio.wav').read_bytes()).hexdigest()
 if h!=fixture['wav_sha256'] or h!=raw['wav_sha256']:raise SystemExit('HASH MISMATCH')
 entries=[]
 for i,r in enumerate(fixture['reference']['records']):
  rows=[]
  for c in raw['result']['raw_frames']:
   if c['ref_index']!=i:continue
   tones=c['tones'];assert len(tones)==79 and set(tones)<=set('01234567')
   blocks=[sum(x==y for x,y in zip(tones[k:k+7],SYNC)) for k in (0,36,72)]
   rows.append(dict(audio_hz=c['audio_hz'],start_pcm_sample=c['frame_start_sample'],sync_hits=sum(blocks),block_hits=blocks,score=c['sync_score'],tones=tones))
  rows.sort(key=lambda q:(q['sync_hits'],q['score']),reverse=True)
  entries.append(dict(reference_index=i,reference_hz=r['audio_hz'],reference_message=r['message_text'],encoded_frame=r['encoded_frame'],candidate_count=len(rows),best=rows[0] if rows else None,all_candidates=rows))
 result=dict(wav_hash=h,entries=entries,limitations=['ref_index originates in prior front-end labeling; no independent matching','tone sequences are hard decisions; may have errors','FEC, CRC and payload not evaluated'])
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 print('WAV SHA256 PASS:',h)
 for e in entries:
  b=e['best'];print(e['reference_hz'],'Hz:',e['candidate_count'],'candidates; best sync:',b['block_hits'] if b else 'NONE')
 print('FEC/CRC/MESSAGE: NOT TESTED')
if __name__=='__main__':main()

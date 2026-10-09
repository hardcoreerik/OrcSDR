#!/usr/bin/env python3
"""Host-only JS8 evidence audit: tone geometry and WAV hash, not FEC/CRC."""
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1])
result=json.loads((root/'orcsdr/docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json').read_text())
fixture=json.loads((root/'dataset/2026-10-08/captures/sample-40m-180s-002/fixture.json').read_text())
wav=root/'dataset/2026-10-08/captures/sample-40m-180s-002/audio.wav'
with wav.open('rb') as stream: sha=hashlib.file_digest(stream,'sha256').hexdigest()
sync=(4,2,5,6,1,3,0);rows=[]
for frame in result['result']['raw_frames']:
    tones=frame['tones']
    assert len(tones)==79 and set(tones)<=set('01234567')
    blocks=[tones[i:i+7] for i in (0,36,72)]
    hits=sum(tone==str(expected) for block in blocks for tone,expected in zip(block,sync))
    data=tones[7:36]+tones[43:72]
    assert len(data)==58
    rows.append(dict(ref_index=frame['ref_index'],audio_hz=frame['audio_hz'],start_sample=frame['frame_start_sample'],sync_hits=hits,data_tones=58,parity='NOT_TESTED',crc='NOT_TESTED',message='NOT_TESTED'))
best={}
for row in rows:
    i=row['ref_index']
    if i not in best or row['sync_hits']>best[i]['sync_hits']:best[i]=row
report=dict(wav_sha256=sha,wav_hash_matches=sha==fixture['wav_sha256'],candidate_rows=len(rows),best_candidates=best,reference_messages=len(fixture['reference']['records']),holdout_ref_index=2,status='PHYSICAL_TONE_STAGE_ONLY',warning='No FEC, CRC, or text verification performed')
print(json.dumps(report,indent=2))
if not report['wav_hash_matches']:sys.exit(2)

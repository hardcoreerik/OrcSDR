#!/usr/bin/env python3
"""Host-only integrity and sync-stage verification for the owner-provided JS8 kit.
No GPL source, parity-check inference, CRC, or message reconstruction.
Usage: python tools/js8-spec/audit_corpus.py /path/to/extracted/core /path/to/extracted/iq /path/to/extracted/reference
"""
import json,hashlib,pathlib,wave,sys
core,iq,reference=map(pathlib.Path,sys.argv[1:4])
base=core/'dataset/2026-10-08'
result=json.loads((core/'orcsdr/docs/js8/results/2026-10-08-sample-40m-180s-002-front-end.json').read_text())
raws=result['result']['raw_frames']
report={'capture_checks':[],'candidate_checks':[]}
for p in sorted((base/'captures').glob('*/fixture.json')):
    fixture=json.loads(p.read_text())
    wav=p.parent/'audio.wav'
    with wav.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
    with wave.open(str(wav)) as stream:
        meta={'channels':stream.getnchannels(),'rate':stream.getframerate(),'width_bytes':stream.getsampwidth(),'frames':stream.getnframes()}
    report['capture_checks'].append({'id':fixture['fixture_id'],'wav_hash_matches':digest==fixture['wav_sha256'],'reference_count':len(fixture['reference']['records']),'reference_output_paths_present':all((base/r['output_path']).exists() for r in fixture['reference']['records']),'wav_format':meta})
sync='4256130'
for r in raws:
    tones=r['tones']
    if len(tones)!=79 or set(tones)-set('01234567'):raise ValueError('invalid tone sequence')
    hits=sum(a==b for start in (0,36,72) for a,b in zip(tones[start:start+7],sync))
    report['candidate_checks'].append({'reference_index':r['ref_index'],'audio_hz':r['audio_hz'],'sample_offset':r['frame_start_sample'],'sync_hits':hits,'data_symbols':len(tones[7:36]+tones[43:72])})
primary=json.loads((base/'captures/sample-40m-180s-002/fixture.json').read_text())
with (iq/'captures/sample-40m-180s-002/raw.cu8').open('rb') as f:report['primary_iq_hash_matches']=hashlib.file_digest(f,'sha256').hexdigest()==primary['raw_sha256']
with (reference/'extracted/usr/bin/js8').open('rb') as f:report['cli_binary_hash_matches']=hashlib.file_digest(f,'sha256').hexdigest()==primary['reference']['cli_binary_sha256']
report['status']='INTEGRITY_AND_SYNC_ONLY'; report['fec_crc_message']='NOT_VERIFIED'
print(json.dumps(report,indent=2))
assert all(x['wav_hash_matches'] and x['reference_output_paths_present'] for x in report['capture_checks'])
assert report['primary_iq_hash_matches'] and report['cli_binary_hash_matches']

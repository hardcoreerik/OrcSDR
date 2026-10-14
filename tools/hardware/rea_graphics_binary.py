#!/usr/bin/env python3
"""Own-built Waveshare 4B ELF REA/Ghidra evidence (no target device access).
Use pinned REA 6.4.0 and Ghidra 12.1.4 via CI; never copy vendor code.
"""
import argparse,datetime,hashlib,json,pathlib,subprocess,sys,shutil

def record(argv,output,stem,timeout=900):
    output.mkdir(parents=True,exist_ok=True)
    (output/(stem+'.command.txt')).write_text(' '.join(map(str,argv))+'\n')
    try:
        p=subprocess.run(argv,capture_output=True,text=True,errors='replace',
                         timeout=timeout,check=False)
        (output/(stem+'.stdout.txt')).write_text(p.stdout)
        (output/(stem+'.stderr.log')).write_text(p.stderr)
        if '--json' in argv:
            try:(output/(stem+'.json')).write_text(json.dumps(json.loads(p.stdout),indent=2)+'\n')
            except ValueError:pass
        print(stem,'exit=',p.returncode)
        return p.returncode==0
    except (OSError,subprocess.TimeoutExpired) as e:
        (output/(stem+'.stderr.log')).write_text(str(e)+'\n')
        print(stem,'failed:',e,file=sys.stderr)
        return False

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',required=True,type=pathlib.Path)
    p.add_argument('--out',required=True,type=pathlib.Path)
    a=p.parse_args()
    if not a.target.is_file():p.error('compiled ELF missing')
    if not shutil.which('rea'):p.error('REA CLI not installed')
    a.out.mkdir(parents=True,exist_ok=True)
    sha=hashlib.sha256(a.target.read_bytes()).hexdigest()
    (a.out/'provenance.json').write_text(json.dumps({
      'target':str(a.target.resolve()),'sha256':sha,
      'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
      'tool':'rea-agents 6.4.0','provider':'Ghidra 12.1.4',
      'scope':'compiled own Waveshare 4B probe ELF; no physical board',
      'disclaimer':'Static binary analysis does not establish hardware correctness'
    },indent=2)+'\n')
    if shutil.which('nm'):
        record(['nm','-C',str(a.target.resolve())],a.out,'symbols',60)
    good=record(['rea','--version'],a.out,'rea-version',30)
    good=record(['rea','doctor','--provider','ghidra','--json'],
                a.out,'rea-doctor',120) and good
    if good:
        good=record(['rea','analyze',str(a.target.resolve()),
                     '--provider','ghidra','--json'],
                    a.out,'rea-native-overview',900)
    return 0 if good else 1
if __name__=='__main__':sys.exit(main())

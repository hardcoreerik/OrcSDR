"""Hardware regression: unauthenticated start and out-of-state install reject.

Does not start a worker, connect ROM, reset C6, or write firmware.
"""
import argparse
import json
from pathlib import Path
import re
import tab5_ft8 as transport


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port",required=True)
    p.add_argument("--baud",type=int,default=921600)
    p.add_argument("--key")
    p.add_argument("--output",type=Path,required=True)
    args=p.parse_args()
    dev=transport.Tab5(args.port,args.baud)
    report={}
    try:
        def query(command,prefix):
            dev.send(command)
            return dev.read_until((prefix,),4,echo=False)
        # Run this check after boot with no previous authenticated session.
        reply=query("RTL_C6_ROM CONNECT","RTL_C6_ROM_ERROR")
        if not any("RTL_C6_ROM_ERROR auth_required" in s for s in reply):
            raise RuntimeError("Unauthenticated start did not reject; no further actions attempted")
        report["unauthenticated_start"]="rejected"
        dev.authenticate(transport.find_key(args.key))
        lines=query("RTL_C6_ROM STATUS","RTL_C6_ROM_STATUS")
        status=dict(re.findall(r"(\w+)=([^\s]+)",next(s for s in lines if "RTL_C6_ROM_STATUS" in s)))
        if status.get("worker_busy")!="0" or status.get("access")!="0":
            raise RuntimeError("Recovery worker already active; invalid-state check not safe")
        reply=query("RTL_C6_ROM RELEASED INSTALL CONFIRM","RTL_C6_ROM_ERROR")
        if not any("invalid_action_or_state" in s for s in reply):
            raise RuntimeError("Out-of-state install did not reject")
        report["install_without_rom_connection"]="rejected"
        lines=query("RTL_C6_ROM STATUS","RTL_C6_ROM_STATUS")
        status=dict(re.findall(r"(\w+)=([^\s]+)",next(s for s in lines if "RTL_C6_ROM_STATUS" in s)))
        if status.get("worker_busy")!="0" or status.get("access")!="0" or status.get("write_enabled")!="0":
            raise RuntimeError("Rejected action changed recovery ownership")
        report.update(status=status,result="PASS")
        print("PASS: auth and phase guards rejected start/install; worker stayed idle")
    finally:
        dev.s.close()
        args.output.write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":main()

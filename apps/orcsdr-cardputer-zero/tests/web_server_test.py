#!/usr/bin/env python3
"""Verify read-only local hosting of immutable, externally supplied Web assets."""
import gzip
import json
import pathlib
import re
import socket
import subprocess
import sys
import tempfile

def request(port,method,path):
    with socket.create_connection(("127.0.0.1",port),timeout=3) as s:
        s.sendall(f"{method} {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode())
        chunks=[]
        while True:
            data=s.recv(65536)
            if not data:
                break
            chunks.append(data)
    header,body=b"".join(chunks).split(b"\r\n\r\n",1)
    return header.decode("ascii"),body

def main():
    binary=sys.argv[1]
    with tempfile.TemporaryDirectory() as td:
        root=pathlib.Path(td)
        html=b"<html><body>Web asset fixture only</body></html>"
        (root/"m5.html.gz").write_bytes(gzip.compress(html))
        (root/"app.js.gz").write_bytes(gzip.compress(b"console.log('test')"))
        (root/"core.wasm.gz").write_bytes(gzip.compress(b"\x00asm\x01\x00\x00\x00"))
        process=subprocess.Popen([binary,"--serve-web",td,"--port","0","--max-requests","7"],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        try:
            line=process.stdout.readline().strip()
            match=re.search(r"loopback_http_listening=127\.0\.0\.1:(\d+)",line)
            assert match, f"Loopback binding failed: {line!r}"
            port=int(match.group(1))
            h,b=request(port,"GET","/api/v1/health")
            assert "200 OK" in h
            state=json.loads(b)
            assert state["loopback_only"] and not state["radio_control"]
            assert not state["radio_service_v1_deployed"]
            h,b=request(port,"GET","/m5")
            assert "200 OK" in h and "Content-Encoding: gzip" in h
            assert gzip.decompress(b)==html
            h,b=request(port,"GET","/m5/app.js")
            assert "text/javascript" in h and gzip.decompress(b)==b"console.log('test')"
            h,b=request(port,"GET","/m5/core.wasm")
            assert "application/wasm" in h and gzip.decompress(b).startswith(b"\x00asm")
            h,b=request(port,"GET","/m5/../README")
            assert "404 Not Found" in h
            h,b=request(port,"POST","/api/v1/receivers/tune")
            assert "405 Method Not Allowed" in h
            h,b=request(port,"GET","/api/v1/receivers")
            assert "501 Not Implemented" in h and b"radio_api_not_implemented" in b
            assert process.wait(timeout=8)==0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    print("Linux loopback Web: valid gzip/WASM, no path traversal or mutation: PASS")

if __name__=="__main__":
    main()

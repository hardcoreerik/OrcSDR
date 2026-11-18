#!/usr/bin/env python3
"""A plain TCP forwarder with an adjustable one-way delay, for testing a client over a long path on a LAN.

  python tools/tcp_delay_proxy.py --listen 192.168.1.15:1240 --target 192.168.1.219:1234 --delay-ms 50

Each direction holds every chunk for --delay-ms before forwarding it (order and throughput preserved), so the
round trip grows by twice the delay. There is no encryption and no authentication: it forwards whatever connects, so
it refuses to listen on 0.0.0.0 and should run only for a test, bound to one LAN address. Not a netem substitute:
it adds delay, not loss or reordering.
"""
import argparse
import asyncio
import sys
import time


async def pump(reader, writer, delay, label, stats):
    queue = asyncio.Queue()

    async def produce():
        try:
            while True:
                data = await reader.read(65536)
                if not data:
                    break
                await queue.put((time.monotonic() + delay, data))
        finally:
            await queue.put(None)

    async def consume():
        try:
            while True:
                item = await queue.get()
                if item is None:
                    break
                release, data = item
                wait = release - time.monotonic()
                if wait > 0:
                    await asyncio.sleep(wait)
                writer.write(data)
                await writer.drain()
                stats[label] += len(data)
        except (ConnectionError, OSError):
            pass
        finally:
            writer.close()

    await asyncio.gather(produce(), consume())


async def handle(client_reader, client_writer, args):
    peer = client_writer.get_extra_info("peername")
    try:
        up_reader, up_writer = await asyncio.open_connection(*args.target)
    except OSError as e:
        print(f"{peer}: cannot reach the target: {e}", flush=True)
        client_writer.close()
        return
    stats = {"to_target": 0, "to_client": 0}
    t0 = time.time()
    print(f"{peer}: connected, one-way delay {args.delay_ms} ms", flush=True)
    delay = args.delay_ms / 1000.0
    await asyncio.gather(pump(client_reader, up_writer, delay, "to_target", stats),
                         pump(up_reader, client_writer, delay, "to_client", stats))
    dt = max(time.time() - t0, 0.001)
    print(f"{peer}: closed after {dt:.0f} s, {stats['to_client']/dt/1000:.0f} kB/s to the client", flush=True)


async def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--listen", required=True, help="HOST:PORT of a LAN address (never 0.0.0.0)")
    p.add_argument("--target", required=True, help="HOST:PORT to forward to")
    p.add_argument("--delay-ms", type=float, default=0.0)
    args = p.parse_args()
    lh, lp = args.listen.rsplit(":", 1)
    th, tp = args.target.rsplit(":", 1)
    if lh in ("0.0.0.0", "::", ""):
        sys.exit("refusing to listen on all interfaces; name one LAN address")
    args.target = (th, int(tp))
    server = await asyncio.start_server(lambda r, w: handle(r, w, args), lh, int(lp))
    print(f"delay proxy on {args.listen} -> {args.target[0]}:{args.target[1]}, +{args.delay_ms} ms each way", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass

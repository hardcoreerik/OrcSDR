"""Remote LoRa test transmitter: send numbered Meshtastic texts from one Heltec.

Run on the laptop that has the Heltec attached, driven over SSH.
  python remote_tx.py COM5 --count 8 --gap 10 --prefix tx1 [--power 5] [--info]
Prints one JSON line per event so the controlling machine can parse it.
"""
import argparse, json, sys, time
import meshtastic.serial_interface as si

p = argparse.ArgumentParser()
p.add_argument("port")
p.add_argument("--count", type=int, default=8)
p.add_argument("--gap", type=float, default=10.0)
p.add_argument("--prefix", nargs="+", default=["rtx"], help="words; joined with spaces so no shell quoting is needed")
p.add_argument("--power", type=int, help="set lora.tx_power (dBm); never 0, which means region maximum")
p.add_argument("--hop-limit", type=int, default=0, help="hop limit for every message (0 keeps the bench from being relayed)")
p.add_argument("--info", action="store_true", help="print region/power and exit")
p.add_argument("--drain", type=float, default=25.0, help="seconds to wait after the last send so the node can transmit its queue")
a = p.parse_args()

if a.power is not None and a.power == 0:
    sys.exit("refusing --power 0 (means region maximum)")

iface = si.SerialInterface(a.port)
time.sleep(4)
lora = iface.localNode.localConfig.lora
print(json.dumps({"event": "info", "region": str(lora.region), "power": lora.tx_power,
                  "preset": str(lora.modem_preset), "tx_enabled": lora.tx_enabled}), flush=True)
if a.power is not None and a.power != lora.tx_power:
    lora.tx_power = a.power
    iface.localNode.writeConfig("lora")
    print(json.dumps({"event": "power_set", "power": a.power}), flush=True)
    iface.close()
    time.sleep(12)  # the node reboots to apply it
    iface = si.SerialInterface(a.port)
    time.sleep(4)
    print(json.dumps({"event": "info", "power": iface.localNode.localConfig.lora.tx_power}), flush=True)
if a.info:
    iface.close()
    sys.exit(0)
def airtime(label):
    dm = (iface.getMyNodeInfo() or {}).get("deviceMetrics", {})
    print(json.dumps({"event": label, "airUtilTx": dm.get("airUtilTx"), "channelUtilization": dm.get("channelUtilization")}), flush=True)
airtime("airtime_before")
for n in range(1, a.count + 1):
    text = f"{' '.join(a.prefix)} {n}"
    iface.sendText(text, hopLimit=a.hop_limit)
    print(json.dumps({"event": "sent", "n": n, "text": text, "t": round(time.time(), 1)}), flush=True)
    time.sleep(a.gap)
time.sleep(a.drain)
airtime("airtime_after")
iface.close()
print(json.dumps({"event": "done"}), flush=True)

import sys, time, json
from pubsub import pub
import meshtastic.serial_interface as si
port, seconds, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
rows = []
def on_receive(packet, interface):
    rows.append({"t": round(time.time(), 1), "from": packet.get("from"), "id": packet.get("id"),
                 "rssi": packet.get("rxRssi"), "snr": packet.get("rxSnr"),
                 "port": (packet.get("decoded") or {}).get("portnum"), "text": (packet.get("decoded") or {}).get("text")})
pub.subscribe(on_receive, "meshtastic.receive")
iface = si.SerialInterface(port)
end = time.time() + seconds
while time.time() < end:
    time.sleep(0.5)
iface.close()
json.dump(rows, open(out, "w"))
print("received", len(rows), "packets")

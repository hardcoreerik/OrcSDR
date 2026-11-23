"""Conservative, independently written interpretations of documented ACARS layouts."""
import re


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc ^ 0xffff


def classify(label, text, tail=''):
    result = {'format': 'unclassified', 'summary': 'As received', 'positions': [], 'events': []}
    def found(name, summary, **details):
        result.update(format=name, summary=summary, **details)
    def position(lat, lon, alt=None, speed=None, mach=None, source='position'):
        if abs(lat) <= 90 and abs(lon) <= 180:
            result['positions'].append(dict(lat=lat, lon=lon, altitude_ft=alt, speed_kt=speed, mach=mach, source=source))
            result['events'].append(('position', {'lat': lat, 'lon': lon}))
    m = re.search(r'/([AB][A-Z0-9]) ([A-Z0-9]{7})\.([A-Z]{2}[A-Z0-9])\.([A-Z0-9]+)$', text)
    if m and tail and m[4].startswith(tail):
        data = m[4][len(tail):]
        if len(data) >= 6 and re.fullmatch('[0-9A-F]+', data):
            body, check = data[:-4], data[-4:]
            packed = bytes.fromhex(body + ('0' if len(body) % 2 else ''))
            if crc16((m[3] + '.' + tail).encode() + packed) == int(check, 16):
                d = dict(direction='downlink' if m[1][0] == 'B' else 'uplink', ground=m[2], application=m[3], registration=tail, payload=body, crc=check)
                found('ARINC 622', f"ATC data link {d['direction']} to {m[2]}, {m[3]}; payload as received: {body} (CRC OK)")
                result['events'].append(('atc link', d)); return result
    m = re.search(r'/([A-Z]{4})\.TI2/(\d{3})([A-Z]{4})([ADCET])([0-9A-F]{4})$', text)
    if label == 'B9' and m and crc16(('TI2/' + m[2] + m[3] + m[4]).encode()) == int(m[5], 16):
        kinds = {'A':'arrival', 'D':'departure', 'C':'arrival with automatic updates', 'E':'en-route service', 'T':'terminate automatic updates'}
        d = dict(station=m[1], airport=m[3], request=kinds[m[4]], width=int(m[2]), crc=m[5])
        found('TI2 ATIS request', f"ATIS request for {m[3]}, {d['request']}, width {m[2]} (CRC OK)")
        result['events'].append(('atis request', d)); return result
    if '#CFBFDE' in text and re.search(r'#CFBFDE\d{6}', text):
        entries = re.findall(r'\b(\d{6})(\d{5})\s+\d+\s+\d+', text)
        found('fault display', 'Fault display list; entries and codes as received')
        result['events'].append(('fault display', {'entries_as_received': entries})); return result
    m = re.search(r'#DFB[^\r\n]*?([A-Z][A-Z0-9-]{2,8}),(\d{6}),([A-Z0-9]{3,7}),([A-Z]{4}),([A-Z]{4})(?:,|$)', text)
    if m:
        found('Southwest DFB header', f'Flight data header: {m[1]}, {m[3]}, {m[4]} to {m[5]}', origin=m[4], destination=m[5]); return result
    samples = list(re.finditer(r'(\d{2})\.(\d{2})\.(\d{2}),(\d{5}),(\d{2,3}\.\d),(\.\d{3})(?=,)', text)) if '#DFB' in text else []
    if samples:
        values = [dict(time=':'.join(x.group(i) for i in (1,2,3)), altitude_ft=int(x[4]), speed_kt=float(x[5]), mach=float(x[6])) for x in samples if int(x[1]) < 24 and int(x[2]) < 60 and int(x[3]) < 60]
        if values:
            found('Southwest DFB samples', 'Flight data samples: ' + '; '.join(f"{v['time']} {v['altitude_ft']} ft, {v['speed_kt']} kt calibrated, Mach {v['mach']}" for v in values), samples=values); return result
    m = re.search(r'#DFB\s*TRP#(\d{6})#([+-]?\d+\.\d+)#([+-]?\d+\.\d+)#(\d{2,3})(?:#|$)', text)
    if m:
        lat, lon = float(m[2]), float(m[3])
        position(lat, lon, int(m[4])*100, source='DFB TRP')
        if result['positions']:
            found('DFB TRP', f'Flight data position {lat:.4f}, {lon:.4f}, {int(m[4])*100} ft'); return result
    m = re.search(r'#DFB\s*[^,]+,(\d{5}),(-?\d+\.\d),[^,]*,[^,]*,([A-Z]{2}),[^,]*,[^,]*,\s*([NS])(\d{2})(\d{2})\.(\d),([EW])(\d{3})(\d{2})\.(\d)', text)
    if m and int(m[6]) < 60 and int(m[10]) < 60:
        lat=(int(m[5])+(int(m[6])+int(m[7])/10)/60)*(-1 if m[4]=='S' else 1)
        lon=(int(m[9])+(int(m[10])+int(m[11])/10)/60)*(-1 if m[8]=='W' else 1)
        position(lat, lon, int(m[1]), source='Southwest DFB climb (altitude probable)')
        if result['positions']:
            found('Southwest DFB climb', f'Position {lat:.4f}, {lon:.4f}; altitude {m[1]} ft and temperature {m[2]} C (probable); phase {m[3]}'); return result
    m = re.search(r'([NS])(\d{2}\.\d{4})\.([EW])(\d{3}\.\d{4})\.(\d{6})\.#(\d{5})\.-?\d+\.\d\.\d{3}\.\d{3}\.[A-Z]{2}#', text) if '#DFB' in text else None
    if m:
        lat=float(m[2])*(-1 if m[1]=='S' else 1); lon=float(m[4])*(-1 if m[3]=='W' else 1)
        position(lat, lon, int(m[6]), source='DFB cruise fix')
        if result['positions']: found('DFB cruise fix', f'Flight data position {lat:.4f}, {lon:.4f}, {m[6]} ft'); return result
    m = re.search(r'#M1BPOS([NS])(\d{2})(\d{3})([EW])(\d{3})(\d{3}),([^,]*),([^,]*),(\d{1,3})(?:,|$)', text)
    if m and int(m[3]) < 600 and int(m[6]) < 600:
        lat=(int(m[2])+int(m[3])/600)*(-1 if m[1]=='S' else 1); lon=(int(m[5])+int(m[6])/600)*(-1 if m[4]=='W' else 1)
        position(lat, lon, int(m[9])*100, source='M1BPOS')
        if result['positions']: found('M1BPOS', f'FMC position {lat:.4f}, {lon:.4f}, {int(m[9])*100} ft; waypoint {m[7]} at {m[8]}'); return result
    m = re.search(r'\b([NS]) ?(\d+\.\d+) ([EW]) ?(\d+\.\d+)', text) if label in ('32','17') else None
    if m:
        lat=float(m[2])*(-1 if m[1]=='S' else 1); lon=float(m[4])*(-1 if m[3]=='W' else 1)
        position(lat, lon, source='label '+label)
        if result['positions']: found('label '+label+' position', f'Position {lat:.4f}, {lon:.4f}; remaining values as received'); return result
    fields=text[10:].split(',')
    if label == '33' and len(fields)>=10 and re.fullmatch(r'\d{2}[A-Z]{3}\d{2}',fields[2]) and re.fullmatch(r'\d{6}', fields[3]) and re.fullmatch(r'[+-]\d+\.\d+',fields[4]) and re.fullmatch(r'[+-]\d+\.\d+',fields[5]):
        lat,lon=float(fields[4]),float(fields[5]); alt=int(fields[6]) if fields[6].isdigit() else None
        position(lat,lon,alt,source='label 33 (altitude probable)')
        if result['positions']:
            route = dict(origin=fields[8],destination=fields[9]) if re.fullmatch('[A-Z]{4}', fields[8]) and re.fullmatch('[A-Z]{4}', fields[9]) else {}
            found('label 33 position', f'Position {lat:.4f}, {lon:.4f}; altitude {alt} ft (probable); other values as received', **route); return result
    return result

"""Provisional ISED SMS CSV staging reader, based on published field dictionary.
Not verified against a real extract. Output is research evidence, not package ABI.
"""
import argparse
import csv
from decimal import Decimal, InvalidOperation, ROUND_HALF_EVEN
import json
import unicodedata


def text(s):
    return unicodedata.normalize('NFC', s.strip()) or None


def number(s):
    try:
        n = Decimal(s.strip())
    except InvalidOperation as e:
        raise ValueError('invalid numeric value') from e
    if not n.is_finite():
        raise ValueError('nonfinite numeric value')
    return n


def row_probe(row, line):
    if len(row) not in (56, 61):
        raise ValueError('unexpected field count')
    direction = text(row[0])
    if direction not in ('TX', 'RX'):
        raise ValueError('unknown station function')
    f = number(row[1]) * 1000000
    if f <= 0 or f > 2**64-1 or f != f.to_integral_value():
        raise ValueError('frequency not representable in positive integer Hz')
    if not text(row[2]):
        raise ValueError('missing frequency record identifier')
    lat, lon = (number(v) if v.strip() else None for v in row[40:42])
    if (lat is not None and abs(lat)>90) or (lon is not None and abs(lon)>180):
        raise ValueError('coordinate out of range')
    coords = None if lat is None or lon is None else [int((v*10000000).to_integral_value(rounding=ROUND_HALF_EVEN)) for v in (lat, lon)]
    return dict(country='CA', source_line=line, frequency_id=text(row[2]), direction=direction,
                frequency_hz=int(f), original_mhz=row[1], regulatory_service=text(row[3]),
                emission=text(row[11]), station=text(row[31]), callsign=text(row[33]),
                station_type=text(row[34]), itu_class=text(row[35]), reference_id=text(row[38]),
                site_e7=coords, original_coordinates=row[40:42], radius_km_raw=text(row[45]),
                authorization_id=text(row[47]), status_raw=text(row[51]), licensee=text(row[54]))


def rows(stream, has_header=False):
    reader = csv.reader(stream, strict=True)
    if has_header:
        header = next(reader)
        if len(header) not in (56, 61) or header[0].strip().casefold() != 'station function' or not header[1].strip().casefold().startswith('frequency'):
            raise ValueError('unrecognized English header; inspect source schema')
    for row in reader:
        try:
            yield row_probe(row, reader.line_num)
        except ValueError as e:
            raise ValueError(f'CSV ending line {reader.line_num}: {e}') from e


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv_file')
    p.add_argument('--encoding', default='utf-8-sig')
    p.add_argument('--header', action='store_true')
    a=p.parse_args()
    with open(a.csv_file, encoding=a.encoding, newline='') as stream:
        for item in rows(stream, a.header):
            print(json.dumps(item, ensure_ascii=False, allow_nan=False))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Compile public-domain Natural Earth 1:110m land to streamed PWM1 coastlines.

Input: ne_110m_land.geojson from nvkelso/natural-earth-vector commit
ca96624a56bd078437bca8184e78163e5039ad19; SHA256
9e0729ee253ca7d7a5c4ae9395fb1902264c5377c52e224d13dd85010e2835d9.
No third-party Python packages. Coordinates quantized to 0.01 degree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

SOURCE_SHA256 = '9e0729ee253ca7d7a5c4ae9395fb1902264c5377c52e224d13dd85010e2835d9'

def compile_world(source, destination):
    raw = Path(source).read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError('Natural Earth source does not match the pinned checksum')
    segments = bytearray()
    for feature in json.loads(raw)['features']:
        geometry = feature['geometry']
        polygons = [geometry['coordinates']] if geometry['type'] == 'Polygon' else geometry['coordinates']
        for polygon in polygons:
            ring = polygon[0]  # Coastlines; inland lakes omitted at this scale.
            previous = tuple(round(v * 100) for v in ring[0])
            for point in ring[1:]:
                current = tuple(round(v * 100) for v in point)
                if current != previous:
                    segments += struct.pack('<hhhh', *previous, *current)
                previous = current
    header = b'PWM1' + struct.pack('<II', len(segments) // 8, zlib.crc32(segments))
    Path(destination).write_bytes(header + segments)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('destination')
    args = parser.parse_args()
    compile_world(args.source, args.destination)

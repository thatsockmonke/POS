#!/usr/bin/env python3
"""Block new release tags while an explicitly requested staged rollout is held."""
import json
from pathlib import Path
import re
import sys

def check(tag, gate):
    if not gate.exists():
        return
    policy = json.loads(gate.read_text())
    if (not isinstance(policy, dict) or set(policy) != {'allowed_tag', 'reason'}
            or not isinstance(policy['allowed_tag'], str)
            or not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', policy['allowed_tag'])
            or not isinstance(policy['reason'], str) or not policy['reason']):
        raise ValueError('Invalid release hold; refuse publication until reviewed')
    if tag != policy['allowed_tag']:
        raise ValueError('Release held: ' + policy['reason'])

if __name__ == '__main__':
    try:
        check(sys.argv[1], Path(sys.argv[2] if len(sys.argv) > 2 else '.github/release-hold.json'))
    except (ValueError, OSError, IndexError) as error:
        raise SystemExit(str(error))

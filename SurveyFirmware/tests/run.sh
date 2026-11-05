#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for mode in ROOMBA HAT; do
  g++ -std=c++17 -DSURVEY_MODE="$mode" -I SurveyFirmware/tests/stubs -x c++ SurveyFirmware/SurveyFirmware.ino SurveyFirmware/src/roomba/*.cpp SurveyFirmware/src/common/*.cpp SurveyFirmware/src/hat/*.cpp SurveyFirmware/tests/stubs/stubs.cpp -o "$tmp/$mode"
done
"$tmp/ROOMBA" $'C,1,RUN\n' $'C,2,PAUSE\n' > "$tmp/roomba.txt"
"$tmp/HAT" $'C,1,HELLO\nC,1,HELLO\nC,2,FEATURES\nC,3,SET,dbg_ms,200\nC,4,DEBUG,ON\nC,5,MIRROR,ON\n' $'C,6,MIRROR,OFF\nC,7,RESET\n' > "$tmp/hat.txt"
python3 - "$tmp" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1]); r = (p/'roomba.txt').read_text(); h = (p/'hat.txt').read_text()
for x in ('K,1,', 'K,2,', 'M,SIMULATION: virtual motors and odometry', 'P,'):
    assert x in r, (x, r[:1000])
assert 'ran 4000 passes' in h
assert h.count('F,tracker,motion,ir,hand_pose,readings,link,heading=buttons') == 3
for ack in ('K,1,Hello from the glove', 'K,2,Features listed', 'K,3,Set dbg_ms', 'K,4,Debug view is off in this build', 'K,5,Debug to app on', 'K,6,Debug to app off', 'K,7,Position reset: this is the start'):
    assert ack in h, ack
before, after = h.split('K,6,Debug to app off', 1)
assert {line.split(',')[1] for line in before.splitlines() if line.startswith('D,')} == {'tracker', 'motion', 'ir', 'hand_pose', 'readings', 'link'}
assert not any(line.startswith('D,') for line in after.splitlines())
poses = [line.split(',') for line in r.splitlines() if line.startswith('P,')]
survey = [p for p in poses if p[8] == 'SURVEY']
paused = [p for p in poses if p[8] == 'PAUSED']
assert survey and paused and int(survey[-1][2]) > 0, (survey[-1:] , paused[-1:])
assert len({(p[2], p[3]) for p in paused}) == 1, paused[-5:]
print('PASS: ROOMBA host commands RUN/PAUSE, virtual movement, telemetry simulation label; HAT host loop')
PY

from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / 'src/roomba/roomba.cpp').read_text()
assert 'sendMessage("SIMULATION: virtual motors and odometry")' in s
assert 'void roombaBegin()' in s and 'void roombaUpdate()' in s
assert 'runCommand(' in s
print('PASS: ROOMBA integration markers')

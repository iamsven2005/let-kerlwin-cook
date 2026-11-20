import os
import subprocess
import sys
from pathlib import Path

import pytest

from ccmaze.nodes import ble_platform as bp

ROOT = Path(__file__).resolve().parents[1]

STOCK = """[General]
#Name = BlueZ

[LE]
# LE default connection parameters.
#MinConnectionInterval=
#MaxConnectionInterval=
#ConnectionLatency=

[Policy]
AutoEnable=true
"""


def test_apply_replaces_commented_keys_in_place():
    out = bp.apply_le_intervals(STOCK)
    assert bp.read_le_intervals(out) == {"MinConnectionInterval": 6, "MaxConnectionInterval": 12}
    assert bp.le_intervals_ok(out)
    # everything else untouched
    assert "#ConnectionLatency=" in out and "AutoEnable=true" in out and "#Name = BlueZ" in out
    assert out.index("MinConnectionInterval=6") < out.index("[Policy]")


def test_apply_adds_missing_section_and_is_idempotent():
    out = bp.apply_le_intervals("[General]\nFoo=1\n")
    assert out.rstrip().endswith("MaxConnectionInterval=12")
    assert "[LE]" in out and bp.le_intervals_ok(out)
    assert bp.apply_le_intervals(out) == out


def test_apply_overrides_existing_values_and_drops_duplicates():
    text = "[LE]\nMinConnectionInterval=24\nMinConnectionInterval=30\n"
    out = bp.apply_le_intervals(text)
    assert out.count("MinConnectionInterval") == 1
    assert bp.le_intervals_ok(out)


def test_keys_in_other_sections_are_ignored():
    text = "[Other]\nMinConnectionInterval=6\nMaxConnectionInterval=12\n"
    assert not bp.le_intervals_ok(text)


def test_error_descriptions():
    class R:
        name = "POWERED_OFF"

    class E(Exception):
        reason = R()

    problem, fix = bp.describe_ble_error(E("x"))
    assert "turned off" in problem and fix
    problem, _ = bp.describe_ble_error(RuntimeError("weird"))
    assert "weird" in problem


def test_windows_client_args_disable_gatt_cache(monkeypatch):
    monkeypatch.setattr(bp, "SYSTEM", "windows")
    assert bp.client_kwargs() == {"winrt": {"use_cached_services": False}}
    monkeypatch.setattr(bp, "SYSTEM", "linux")
    assert bp.client_kwargs() == {}


@pytest.mark.skipif(not sys.platform.startswith("linux") or os.geteuid() != 0, reason="needs Linux + root")
def test_bluez_tool_apply_and_undo(tmp_path):
    conf = tmp_path / "main.conf"
    conf.write_text(STOCK)
    tool = [sys.executable, str(ROOT / "tools" / "bluez_conn_interval.py")]
    st = subprocess.run(tool + ["status", "--quiet", "--conf", str(conf)], capture_output=True, text=True)
    assert st.stdout.strip() == "unset"
    subprocess.run(tool + ["apply", "--no-restart", "--conf", str(conf)], check=True, capture_output=True)
    assert bp.le_intervals_ok(conf.read_text())
    assert list(tmp_path.glob("main.conf.ccmaze-backup-*"))
    subprocess.run(tool + ["undo", "--no-restart", "--conf", str(conf)], check=True, capture_output=True)
    assert conf.read_text() == STOCK


def test_doctor_runs_without_crashing():
    r = subprocess.run([sys.executable, str(ROOT / "doctor.py"), "--no-scan"], capture_output=True, text=True,
                       timeout=60)
    assert r.returncode in (0, 1)
    assert "environment check" in r.stdout and "Python" in r.stdout

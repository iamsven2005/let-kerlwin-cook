import json

from ccmaze.config import DEFAULT_CONFIG, Config


def test_defaults_match_brief_v12():
    for k in ("lost_after_missed_pongs", "battery_warn_percent", "flip_commit_window_ms",
              "manual_auto_face_up", "maze_algorithm"):
        assert k in DEFAULT_CONFIG
    assert DEFAULT_CONFIG["ble_service_uuid"] == "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    assert "ble_max_missed_pongs" not in DEFAULT_CONFIG


def test_old_key_is_migrated(tmp_path):
    p = tmp_path / "config.json"
    p.write_text(json.dumps({"ble_max_missed_pongs": 5, "maze_size": 11}))
    c = Config.load(p)
    assert c.lost_after_missed_pongs == 5 and c.maze_size == 11
    c.save()
    saved = json.loads(p.read_text())
    assert "ble_max_missed_pongs" not in saved and saved["lost_after_missed_pongs"] == 5


def test_saved_config_has_no_absolute_paths(tmp_path):
    p = tmp_path / "config.json"
    Config({}, p).save()
    text = p.read_text()
    assert str(tmp_path) not in text
    assert json.loads(text)["card_catalog_file"] == "cards.json"

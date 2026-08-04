from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TIMER = (ROOT / "src" / "ArcadeTimer.cpp").read_text(encoding="utf-8")
DISPLAY = (ROOT / "src" / "DisplayManager.cpp").read_text(encoding="utf-8")
BUTTONS = (ROOT / "src" / "PeripheryManager.cpp").read_text(encoding="utf-8")


def test_button_timing_and_adjustments_are_device_local() -> None:
    assert "kDebounceMs = 120" in TIMER
    assert "kDoublePressMs = 800" in TIMER
    assert "adjust(-60);" in TIMER
    assert "adjust(60);" in TIMER
    assert "remaining <= static_cast<uint32_t>(-seconds)" in TIMER
    assert "ArcadeTimer.handleCenter();" in BUTTONS


def test_timer_owns_display_before_normal_rotation() -> None:
    timer_branch = DISPLAY.index("else if (ArcadeTimer.ownsDisplay())")
    normal_rotation = DISPLAY.index("ui->update();", timer_branch)
    assert timer_branch < normal_rotation


def test_recovery_and_capability_contract() -> None:
    assert 'doc["authority"] = "device"' in TIMER
    for feature in (
        "local_countdown",
        "local_buttons",
        "local_alarm",
        "plus_one",
        "minus_one",
        "recovery",
    ):
        assert f'"{feature}"' in TIMER
    assert "kRecoveryWindowSeconds = 10 * 60" in TIMER
    assert "deadlineEpoch = expiredEpoch;" in TIMER


def test_upstream_ota_cannot_replace_custom_firmware() -> None:
    mqtt = (ROOT / "src" / "MQTTManager.cpp").read_text(encoding="utf-8")
    updater = (ROOT / "src" / "UpdateManager.cpp").read_text(encoding="utf-8")
    assert "Remote upstream update disabled" in mqtt
    assert "Upstream OTA is locked" in updater

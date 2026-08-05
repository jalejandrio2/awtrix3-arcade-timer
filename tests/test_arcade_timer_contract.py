from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TIMER = (ROOT / "src" / "ArcadeTimer.cpp").read_text(encoding="utf-8")
DISPLAY = (ROOT / "src" / "DisplayManager.cpp").read_text(encoding="utf-8")
BUTTONS = (ROOT / "src" / "PeripheryManager.cpp").read_text(encoding="utf-8")
MANIFEST_BUILDER = (ROOT / "scripts" / "check_firmware.py").read_text(encoding="utf-8")


def test_button_timing_and_adjustments_are_device_local() -> None:
    assert "kDebounceMs = 120" in TIMER
    assert "kDoublePressMs = 800" in TIMER
    assert "adjust(-60);" in TIMER
    assert "adjust(60);" in TIMER
    assert "remaining <= static_cast<uint32_t>(-seconds)" in TIMER
    assert "ArcadeTimer.handleCenter();" in BUTTONS
    assert "resetReady();" in TIMER
    assert "state = ArcadeTimerState::Paused;" in TIMER
    assert 'MQTTManager.setCurrentApp("ArcadeTimer");' in TIMER


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
        "wake_display_restore",
    ):
        assert f'"{feature}"' in TIMER
    assert "kRecoveryWindowSeconds = 10 * 60" in TIMER
    assert "deadlineEpoch = expiredEpoch;" in TIMER


def test_completion_alarm_auto_dismisses_after_ten_seconds() -> None:
    assert "kAlarmDurationMs = 10 * 1000" in TIMER
    assert "ringingEndsMs = millis() + kAlarmDurationMs" in TIMER
    assert "nowMs - ringingEndsMs" in TIMER
    assert "dismiss();" in TIMER
    assert 'kFirmwareVersion = "0.98-arcade.3"' in TIMER
    assert '"version": "0.98-arcade.3"' in MANIFEST_BUILDER


def test_timer_wakes_and_restores_an_initially_dark_display() -> None:
    assert "restoreDisplayOff = MATRIX_OFF;" in TIMER
    assert "DisplayManager.setPower(true);" in TIMER
    assert 'doc["restore_display_off"] = restoreDisplayOff;' in TIMER
    assert 'timerPreferences.putBool("restore_off", restoreDisplayOff);' in TIMER
    assert "ArcadeTimer.protectsDisplayPower()" in DISPLAY
    assert "DisplayManager.setPower(false);" in TIMER


def test_upstream_ota_cannot_replace_custom_firmware() -> None:
    mqtt = (ROOT / "src" / "MQTTManager.cpp").read_text(encoding="utf-8")
    updater = (ROOT / "src" / "UpdateManager.cpp").read_text(encoding="utf-8")
    assert "Remote upstream update disabled" in mqtt
    assert "Upstream OTA is locked" in updater

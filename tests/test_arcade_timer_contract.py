from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TIMER = (ROOT / "src" / "ArcadeTimer.cpp").read_text(encoding="utf-8")
TIMER_HEADER = (ROOT / "src" / "ArcadeTimer.h").read_text(encoding="utf-8")
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
    assert "ArcadeTimer.handleCenterLong();" in BUTTONS
    assert "button_select.onPressedFor(1000, select_button_pressed_long);" in BUTTONS
    assert "bool ArcadeTimerManager::handleCenterLong()" in TIMER
    assert "handleCenterLong()\n{\n    if (state == ArcadeTimerState::Idle" in TIMER
    assert "dismiss();\n    return true;" in TIMER
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
        "hold_to_exit",
        "usage_queue",
    ):
        assert f'"{feature}"' in TIMER
    assert "kRecoveryWindowSeconds = 10 * 60" in TIMER
    assert "deadlineEpoch = expiredEpoch;" in TIMER


def test_completion_alarm_auto_dismisses_after_ten_seconds() -> None:
    assert "kAlarmDurationMs = 10 * 1000" in TIMER
    assert "ringingEndsMs = millis() + kAlarmDurationMs" in TIMER
    assert "nowMs - ringingEndsMs" in TIMER
    assert "dismiss();" in TIMER
    assert 'kFirmwareVersion = "0.98-arcade.5"' in TIMER
    assert '"version": "0.98-arcade.5"' in MANIFEST_BUILDER


def test_timer_usage_is_transition_only_and_durable() -> None:
    assert "publishState(false)" not in TIMER
    assert "lastPublishedSecond" not in TIMER
    assert "kUsageQueueCapacity = 128" in TIMER_HEADER
    assert 'MQTTManager.publishRetained("stats/timer/usage/status"' in TIMER
    assert '"source_session_id"' in TIMER
    assert '"active_seconds"' in TIMER
    assert '"timing_quality"' in TIMER
    assert "acknowledgeUsage" in TIMER
    assert 'timerPreferences.putBytes("usage_queue"' in TIMER


def test_active_usage_excludes_pauses_and_alarm_time() -> None:
    assert "stopActiveSegment();\n    deadlineUs = 0;" in TIMER
    assert 'finalizeSession("completed", expiredEpoch);' in TIMER
    assert 'finalizeSession("cancelled");' in TIMER
    assert "(activeMillis + 500) / 1000" in TIMER


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

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GLOBALS = (ROOT / "src" / "Globals.cpp").read_text(encoding="utf-8")
GLOBALS_HEADER = (ROOT / "src" / "Globals.h").read_text(encoding="utf-8")
SERVER = (ROOT / "src" / "ServerManager.cpp").read_text(encoding="utf-8")
DISPLAY = (ROOT / "src" / "DisplayManager.cpp").read_text(encoding="utf-8")
UI = (ROOT / "src" / "MatrixDisplayUi.cpp").read_text(encoding="utf-8")
PERIPHERY = (ROOT / "src" / "PeripheryManager.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")


def test_radio_uses_modem_sleep_and_adaptive_transmit_caps() -> None:
    assert "esp_wifi_set_ps(WIFI_PS_MIN_MODEM)" in SERVER
    assert "WIFI_PS_NONE" not in SERVER
    assert "kTxPower11Dbm = 44" in SERVER
    assert "kTxPower15Dbm = 60" in SERVER
    assert "kTxPower19_5Dbm = 78" in SERVER
    for threshold in ("rssi >= -60", "rssi >= -72", "rssi < -65", "rssi < -77", "rssi >= -55", "rssi >= -67"):
        assert threshold in SERVER
    assert "kRadioReviewIntervalMs = 60000" in SERVER
    assert "reconnectFallback = true" in SERVER
    assert "!MQTTManager.isConnected()" in SERVER


def test_cpu_loop_and_visual_target_are_reduced_without_sleeping() -> None:
    assert "board_build.f_cpu = 160000000L" in PLATFORMIO
    assert "setCpuFrequencyMhz(160);" in MAIN
    assert "delay(1);" in MAIN
    assert "uint8_t MATRIX_FPS = 30;" in GLOBALS
    assert "deepSleep" not in MAIN


def test_sampling_and_statistics_cadence_are_battery_optimized() -> None:
    assert "LDR_SAMPLE_INTERVAL_MS = 1000" in GLOBALS_HEADER
    assert "BATTERY_ENVIRONMENT_SAMPLE_INTERVAL_MS = 30000" in GLOBALS_HEADER
    assert "BRIGHTNESS_HYSTERESIS = 3" in GLOBALS_HEADER
    assert "interval_BatTempHum = BATTERY_ENVIRONMENT_SAMPLE_INTERVAL_MS" in PERIPHERY
    assert "interval_LDR = LDR_SAMPLE_INTERVAL_MS" in PERIPHERY
    assert "abs(targetBrightness - BRIGHTNESS) >= BRIGHTNESS_HYSTERESIS" in PERIPHERY
    assert "long STATS_INTERVAL = 60000;" in GLOBALS


def test_led_transmission_deduplicates_frames_and_reports_diagnostics() -> None:
    assert "memcmp(lastTransmittedLeds, leds, sizeof(leds))" in DISPLAY
    assert "++FRAMES_SENT;" in DISPLAY
    assert "++FRAMES_SKIPPED;" in DISPLAY
    assert "DisplayManager.show();" in UI
    assert "power.createNestedObject" in DISPLAY
    for key in (
        "wifi_ps",
        "tx_power_dbm",
        "cpu_mhz",
        "target_fps",
        "effective_fps",
        "frames_sent",
        "frames_skipped",
        "effective_brightness",
        "sampling_ms",
    ):
        assert f'F("{key}")' in DISPLAY


def test_matrix_off_rendering_is_throttled_but_timer_stays_live() -> None:
    assert "now - lastOffUiUpdate >= 1000" in DISPLAY
    assert "ArcadeTimer.tick();" in DISPLAY
    assert "ArcadeTimer.render(matrix);" in DISPLAY
    assert "1000UL / MATRIX_FPS" in DISPLAY

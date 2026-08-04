#include "ArcadeTimer.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>

#include "Globals.h"
#include "MQTTManager.h"
#include "PeripheryManager.h"

namespace
{
constexpr uint32_t kDebounceMs = 120;
constexpr uint32_t kDoublePressMs = 800;
constexpr uint32_t kMaximumSeconds = 120 * 60;
constexpr time_t kValidEpoch = 1700000000;
constexpr time_t kRecoveryWindowSeconds = 10 * 60;
constexpr const char *kFirmwareVersion = "0.98-arcade.1";

Preferences timerPreferences;

const uint8_t digits5x7[10][7] PROGMEM = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}};

const uint8_t digits3x5[10][5] PROGMEM = {
    {0x07, 0x05, 0x05, 0x05, 0x07}, {0x02, 0x06, 0x02, 0x02, 0x07},
    {0x07, 0x01, 0x07, 0x04, 0x07}, {0x07, 0x01, 0x07, 0x01, 0x07},
    {0x05, 0x05, 0x07, 0x01, 0x01}, {0x07, 0x04, 0x07, 0x01, 0x07},
    {0x07, 0x04, 0x07, 0x05, 0x07}, {0x07, 0x01, 0x01, 0x01, 0x01},
    {0x07, 0x05, 0x07, 0x05, 0x07}, {0x07, 0x05, 0x07, 0x01, 0x07}};

void drawDigit5(FastLED_NeoMatrix *matrix, uint8_t digit, int16_t x, int16_t y, uint32_t color)
{
    for (uint8_t row = 0; row < 7; ++row)
    {
        uint8_t bits = pgm_read_byte(&digits5x7[digit][row]);
        for (uint8_t col = 0; col < 5; ++col)
            if (bits & (1 << (4 - col)))
                matrix->drawPixel(x + col, y + row, color);
    }
}

void drawDigit3(FastLED_NeoMatrix *matrix, uint8_t digit, int16_t x, int16_t y, uint32_t color)
{
    for (uint8_t row = 0; row < 5; ++row)
    {
        uint8_t bits = pgm_read_byte(&digits3x5[digit][row]);
        for (uint8_t col = 0; col < 3; ++col)
            if (bits & (1 << (2 - col)))
                matrix->drawPixel(x + col, y + row, color);
    }
}

void drawTinySymbol(FastLED_NeoMatrix *matrix, char symbol, int16_t x, int16_t y, uint32_t color)
{
    if (symbol == '+')
    {
        for (int i = 0; i < 5; ++i)
            matrix->drawPixel(x + 2, y + i, color);
        for (int i = 0; i < 5; ++i)
            matrix->drawPixel(x + i, y + 2, color);
    }
    else
    {
        for (int i = 0; i < 5; ++i)
            matrix->drawPixel(x + i, y + 2, color);
    }
}

uint32_t urgencyColor(uint32_t remaining)
{
    if (remaining <= 10)
        return 0xFF2D2D;
    if (remaining <= 60)
        return 0xFF9F1C;
    return 0x00F5D4;
}
} // namespace

ArcadeTimerManager ArcadeTimer;

void ArcadeTimerManager::setup()
{
    bootId = esp_random();
    restore();
}

bool ArcadeTimerManager::timeValid() const
{
    return time(nullptr) >= kValidEpoch;
}

uint32_t ArcadeTimerManager::remainingSeconds() const
{
    if (state != ArcadeTimerState::Running)
        return pausedSeconds;
    int64_t remainingUs = deadlineUs - esp_timer_get_time();
    if (remainingUs <= 0)
        return 0;
    return static_cast<uint32_t>((remainingUs + 999999) / 1000000);
}

const char *ArcadeTimerManager::stateName() const
{
    switch (state)
    {
    case ArcadeTimerState::Running:
        return "running";
    case ArcadeTimerState::Paused:
        return "paused";
    case ArcadeTimerState::Ringing:
        return "ringing";
    case ArcadeTimerState::Recovering:
        return "recovering";
    default:
        return "idle";
    }
}

bool ArcadeTimerManager::ownsDisplay() const
{
    return state != ArcadeTimerState::Idle || feedback != Feedback::None || testAlarmActive;
}

void ArcadeTimerManager::tick()
{
    uint32_t nowMs = millis();
    if (feedback != Feedback::None && nowMs - feedbackStartedMs >= feedbackDurationMs)
        feedback = Feedback::None;

    if (state == ArcadeTimerState::Recovering && timeValid())
    {
        time_t nowEpoch = time(nullptr);
        if (deadlineEpoch > nowEpoch)
        {
            pausedSeconds = static_cast<uint32_t>(deadlineEpoch - nowEpoch);
            deadlineUs = esp_timer_get_time() + static_cast<int64_t>(pausedSeconds) * 1000000;
            state = ArcadeTimerState::Running;
        }
        else if (deadlineEpoch > 0 && nowEpoch - deadlineEpoch <= kRecoveryWindowSeconds)
        {
            expiredEpoch = deadlineEpoch;
            state = ArcadeTimerState::Ringing;
            pausedSeconds = 0;
        }
        else
        {
            state = ArcadeTimerState::Idle;
            pausedSeconds = defaultSeconds;
            totalSeconds = defaultSeconds;
        }
        persist();
        publishState(true);
    }

    if (state == ArcadeTimerState::Running)
    {
        uint32_t remaining = remainingSeconds();
        if (!recoverable && timeValid())
        {
            deadlineEpoch = time(nullptr) + remaining;
            recoverable = true;
            persist();
        }
        if (remaining == 0)
            complete();
        else if (remaining != lastPublishedSecond)
        {
            lastPublishedSecond = remaining;
            publishState(false);
        }
    }

    if (testAlarmActive && static_cast<int32_t>(nowMs - testAlarmEndsMs) >= 0)
        stopTestAlarm();

    updateAlarm();
}

void ArcadeTimerManager::start()
{
    totalSeconds = defaultSeconds;
    pausedSeconds = defaultSeconds;
    deadlineUs = esp_timer_get_time() + static_cast<int64_t>(defaultSeconds) * 1000000;
    recoverable = timeValid();
    deadlineEpoch = recoverable ? time(nullptr) + defaultSeconds : 0;
    expiredEpoch = 0;
    state = ArcadeTimerState::Running;
    setFeedback(Feedback::Start, 350);
    persist();
    publishState(true);
    MQTTManager.setCurrentApp("ArcadeTimer");
}

void ArcadeTimerManager::pause()
{
    if (state != ArcadeTimerState::Running)
        return;
    pausedSeconds = remainingSeconds();
    deadlineUs = 0;
    deadlineEpoch = 0;
    state = ArcadeTimerState::Paused;
    persist();
    publishState(true);
}

void ArcadeTimerManager::resume()
{
    if (state != ArcadeTimerState::Paused)
        return;
    deadlineUs = esp_timer_get_time() + static_cast<int64_t>(pausedSeconds) * 1000000;
    recoverable = timeValid();
    deadlineEpoch = recoverable ? time(nullptr) + pausedSeconds : 0;
    state = ArcadeTimerState::Running;
    setFeedback(Feedback::Resume, 250);
    persist();
    publishState(true);
}

void ArcadeTimerManager::cancel()
{
    PeripheryManager.stopSound();
    alarmPlaying = false;
    state = ArcadeTimerState::Idle;
    totalSeconds = defaultSeconds;
    pausedSeconds = defaultSeconds;
    deadlineUs = 0;
    deadlineEpoch = 0;
    expiredEpoch = 0;
    recoverable = false;
    centerCandidate = false;
    setFeedback(Feedback::Cancel, 450);
    persist();
    publishState(true);
}

void ArcadeTimerManager::complete()
{
    state = ArcadeTimerState::Ringing;
    pausedSeconds = 0;
    expiredEpoch = deadlineEpoch > 0 ? deadlineEpoch : (timeValid() ? time(nullptr) : 0);
    deadlineUs = 0;
    deadlineEpoch = 0;
    centerCandidate = false;
    persist();
    publishState(true);
}

void ArcadeTimerManager::dismiss()
{
    PeripheryManager.stopSound();
    alarmPlaying = false;
    state = ArcadeTimerState::Idle;
    totalSeconds = defaultSeconds;
    pausedSeconds = defaultSeconds;
    deadlineUs = 0;
    deadlineEpoch = 0;
    expiredEpoch = 0;
    recoverable = false;
    centerCandidate = false;
    persist();
    publishState(true);
}

void ArcadeTimerManager::adjust(int32_t seconds)
{
    if (state != ArcadeTimerState::Running && state != ArcadeTimerState::Paused)
        return;
    uint32_t remaining = remainingSeconds();
    if (seconds < 0 && remaining <= static_cast<uint32_t>(-seconds))
    {
        complete();
        return;
    }
    int32_t adjusted = static_cast<int32_t>(remaining) + seconds;
    adjusted = constrain(adjusted, 1, static_cast<int32_t>(kMaximumSeconds));
    pausedSeconds = static_cast<uint32_t>(adjusted);
    totalSeconds = constrain(static_cast<int32_t>(totalSeconds) + seconds, adjusted, static_cast<int32_t>(kMaximumSeconds));
    if (state == ArcadeTimerState::Running)
    {
        deadlineUs = esp_timer_get_time() + static_cast<int64_t>(pausedSeconds) * 1000000;
        deadlineEpoch = timeValid() ? time(nullptr) + pausedSeconds : 0;
        recoverable = deadlineEpoch > 0;
    }
    setFeedback(seconds > 0 ? Feedback::PlusOne : Feedback::MinusOne, 450);
    persist();
    publishState(true);
}

bool ArcadeTimerManager::handleCenter()
{
    uint32_t nowMs = millis();
    if (testAlarmActive)
    {
        stopTestAlarm();
        return true;
    }
    if (state == ArcadeTimerState::Ringing)
    {
        dismiss();
        return true;
    }
    if (state == ArcadeTimerState::Recovering)
        return true;
    if (state == ArcadeTimerState::Idle)
    {
        start();
        centerCandidate = true;
        lastCenterMs = nowMs;
        return true;
    }
    if (centerCandidate && nowMs - lastCenterMs < kDebounceMs)
        return true;
    if (centerCandidate && nowMs - lastCenterMs <= kDoublePressMs)
    {
        cancel();
        return true;
    }
    if (state == ArcadeTimerState::Running)
        pause();
    else if (state == ArcadeTimerState::Paused)
        resume();
    centerCandidate = true;
    lastCenterMs = nowMs;
    return true;
}

bool ArcadeTimerManager::handleLeft()
{
    centerCandidate = false;
    if (state == ArcadeTimerState::Running || state == ArcadeTimerState::Paused)
    {
        adjust(-60);
        return true;
    }
    return state == ArcadeTimerState::Ringing || state == ArcadeTimerState::Recovering;
}

bool ArcadeTimerManager::handleRight()
{
    centerCandidate = false;
    if (state == ArcadeTimerState::Running || state == ArcadeTimerState::Paused)
    {
        adjust(60);
        return true;
    }
    return state == ArcadeTimerState::Ringing || state == ArcadeTimerState::Recovering;
}

void ArcadeTimerManager::setFeedback(Feedback next, uint32_t durationMs)
{
    feedback = next;
    feedbackStartedMs = millis();
    feedbackDurationMs = durationMs;
}

const char *ArcadeTimerManager::melodyRtttl() const
{
    if (melody == "alarm")
        return "Alarm:d=8,o=6,b=180:c7,p,c7,p,g,p,g,p";
    if (melody == "chime")
        return "Chime:d=8,o=6,b=140:c,e,g,4c7";
    return "Arcade:d=16,o=6,b=180:c,e,g,8c7,g,e,c,8g5,c,e,g,4c7";
}

void ArcadeTimerManager::updateAlarm()
{
    bool shouldPlay = alarmEnabled && (state == ArcadeTimerState::Ringing || testAlarmActive);
    if (shouldPlay && !PeripheryManager.isPlaying())
    {
        PeripheryManager.playRTTTLString(melodyRtttl());
        alarmPlaying = true;
    }
    else if (!shouldPlay && alarmPlaying)
    {
        PeripheryManager.stopSound();
        alarmPlaying = false;
    }
}

void ArcadeTimerManager::testAlarm(const char *json)
{
    if (state != ArcadeTimerState::Idle || testAlarmActive)
        return;
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json))
        return;
    testAlarmActive = true;
    testAlarmEndsMs = millis() + 5000;
}

void ArcadeTimerManager::stopTestAlarm()
{
    testAlarmActive = false;
    PeripheryManager.stopSound();
    alarmPlaying = false;
}

void ArcadeTimerManager::applyConfig(const char *json)
{
    StaticJsonDocument<384> doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error || doc["schema"].as<int>() != 1)
    {
        publishConfigAck("rejected", "invalid_schema_or_json");
        return;
    }
    uint32_t revision = doc["revision"] | 0;
    int minutes = doc["default_minutes"] | 0;
    String nextStyle = doc["animation_style"] | "";
    String nextMelody = doc["melody"] | "";
    if (revision < configRevision || minutes < 1 || minutes > 120 ||
        (nextStyle != "playful_arcade" && nextStyle != "clean" && nextStyle != "soft") ||
        (nextMelody != "arcade" && nextMelody != "alarm" && nextMelody != "chime"))
    {
        publishConfigAck("rejected", "invalid_config");
        return;
    }
    configRevision = revision;
    defaultSeconds = static_cast<uint32_t>(minutes) * 60;
    animationStyle = nextStyle;
    melody = nextMelody;
    alarmEnabled = doc["alarm_enabled"] | true;
    if (state == ArcadeTimerState::Idle)
    {
        totalSeconds = defaultSeconds;
        pausedSeconds = defaultSeconds;
    }
    persist();
    publishConfigAck("applied");
    publishState(true);
}

void ArcadeTimerManager::persist()
{
    timerPreferences.begin("arcade_timer", false);
    timerPreferences.putUChar("state", static_cast<uint8_t>(state));
    timerPreferences.putUInt("default", defaultSeconds);
    timerPreferences.putUInt("total", totalSeconds);
    timerPreferences.putUInt("paused", remainingSeconds());
    timerPreferences.putLong64("deadline", static_cast<int64_t>(deadlineEpoch));
    timerPreferences.putLong64("expired", static_cast<int64_t>(expiredEpoch));
    timerPreferences.putBool("recoverable", recoverable);
    timerPreferences.putUInt("config_rev", configRevision);
    timerPreferences.putBool("alarm", alarmEnabled);
    timerPreferences.putString("animation", animationStyle);
    timerPreferences.putString("melody", melody);
    timerPreferences.end();
}

void ArcadeTimerManager::restore()
{
    timerPreferences.begin("arcade_timer", true);
    defaultSeconds = timerPreferences.getUInt("default", 300);
    totalSeconds = timerPreferences.getUInt("total", defaultSeconds);
    pausedSeconds = timerPreferences.getUInt("paused", defaultSeconds);
    deadlineEpoch = static_cast<time_t>(timerPreferences.getLong64("deadline", 0));
    expiredEpoch = static_cast<time_t>(timerPreferences.getLong64("expired", 0));
    recoverable = timerPreferences.getBool("recoverable", false);
    configRevision = timerPreferences.getUInt("config_rev", 0);
    alarmEnabled = timerPreferences.getBool("alarm", true);
    animationStyle = timerPreferences.getString("animation", "playful_arcade");
    melody = timerPreferences.getString("melody", "arcade");
    state = static_cast<ArcadeTimerState>(timerPreferences.getUChar("state", 0));
    timerPreferences.end();

    if (state == ArcadeTimerState::Running)
        state = recoverable && deadlineEpoch > 0 ? ArcadeTimerState::Recovering : ArcadeTimerState::Idle;
    else if (state == ArcadeTimerState::Ringing)
    {
        deadlineEpoch = expiredEpoch;
        state = expiredEpoch > 0 ? ArcadeTimerState::Recovering : ArcadeTimerState::Ringing;
    }
    else if (state != ArcadeTimerState::Paused)
        state = ArcadeTimerState::Idle;
    if (state == ArcadeTimerState::Idle)
    {
        totalSeconds = defaultSeconds;
        pausedSeconds = defaultSeconds;
    }
}

void ArcadeTimerManager::publishCapability()
{
    StaticJsonDocument<384> doc;
    doc["schema"] = 1;
    doc["authority"] = "device";
    doc["firmware"] = kFirmwareVersion;
    doc["max_minutes"] = 120;
    JsonArray features = doc.createNestedArray("features");
    for (const char *feature : {"local_countdown", "local_buttons", "local_alarm", "plus_one", "minus_one", "recovery"})
        features.add(feature);
    String payload;
    serializeJson(doc, payload);
    MQTTManager.publishRetained("stats/timer/capability", payload.c_str());
}

void ArcadeTimerManager::publishState(bool retained)
{
    StaticJsonDocument<512> doc;
    doc["schema"] = 1;
    doc["boot_id"] = bootId;
    doc["sequence"] = ++sequence;
    doc["status"] = stateName();
    doc["total_seconds"] = totalSeconds;
    doc["remaining_seconds"] = remainingSeconds();
    if (deadlineEpoch > 0)
        doc["deadline_epoch"] = static_cast<int64_t>(deadlineEpoch);
    else
        doc["deadline_epoch"] = nullptr;
    doc["recoverable"] = recoverable;
    doc["config_revision"] = configRevision;
    doc["firmware"] = kFirmwareVersion;
    doc["updated_at_epoch"] = timeValid() ? static_cast<int64_t>(time(nullptr)) : 0;
    String payload;
    serializeJson(doc, payload);
    if (retained)
        MQTTManager.publishRetained("stats/timer", payload.c_str());
    else
        MQTTManager.publish("stats/timer", payload.c_str());
}

void ArcadeTimerManager::publishConfigAck(const char *status, const char *error)
{
    StaticJsonDocument<192> doc;
    doc["schema"] = 1;
    doc["applied_revision"] = configRevision;
    doc["status"] = status;
    if (error)
        doc["error"] = error;
    else
        doc["error"] = nullptr;
    String payload;
    serializeJson(doc, payload);
    MQTTManager.publishRetained("stats/timer/config", payload.c_str());
}

void ArcadeTimerManager::onMqttConnected()
{
    publishCapability();
    publishConfigAck("applied");
    publishState(true);
}

void ArcadeTimerManager::drawIdle(FastLED_NeoMatrix *matrix, int16_t x, int16_t y)
{
    uint32_t color = 0xFFCF33;
    bool flip = (millis() / 500) % 2;
    for (int i = 0; i < 8; ++i)
    {
        matrix->drawPixel(x + i, y, color);
        matrix->drawPixel(x + i, y + 7, color);
    }
    for (int i = 1; i < 7; ++i)
    {
        matrix->drawPixel(x + i, y + i, color);
        matrix->drawPixel(x + 7 - i, y + i, color);
    }
    matrix->drawPixel(x + 3, y + (flip ? 5 : 2), static_cast<uint32_t>(0xFFFFFF));
    matrix->drawPixel(x + 4, y + (flip ? 5 : 2), static_cast<uint32_t>(0xFFFFFF));

    uint32_t minutes = defaultSeconds / 60;
    char label[6];
    snprintf(label, sizeof(label), "%02lu00", static_cast<unsigned long>(minutes));
    int positions[4] = {11, 15, 22, 26};
    for (int i = 0; i < 4; ++i)
        drawDigit3(matrix, label[i] - '0', x + positions[i], y + 1, 0xFFFFFF);
    matrix->drawPixel(x + 19, y + 2, static_cast<uint32_t>(0xFFFFFF));
    matrix->drawPixel(x + 19, y + 4, static_cast<uint32_t>(0xFFFFFF));
}

void ArcadeTimerManager::drawCountdown(FastLED_NeoMatrix *matrix, int16_t x, int16_t y)
{
    uint32_t remaining = remainingSeconds();
    uint32_t color = state == ArcadeTimerState::Paused ? 0x3498DB : urgencyColor(remaining);
    if (state == ArcadeTimerState::Paused && animationStyle != "clean")
    {
        uint8_t scale = 110 + (sin8(millis() / 8) / 2);
        CRGB breathing(color);
        breathing.nscale8_video(scale);
        color = (breathing.r << 16) | (breathing.g << 8) | breathing.b;
    }
    uint32_t minutes = remaining / 60;
    uint32_t seconds = remaining % 60;
    if (minutes < 100)
    {
        drawDigit5(matrix, (minutes / 10) % 10, x + 2, y, color);
        drawDigit5(matrix, minutes % 10, x + 8, y, color);
        drawDigit5(matrix, seconds / 10, x + 19, y, color);
        drawDigit5(matrix, seconds % 10, x + 25, y, color);
        if (state == ArcadeTimerState::Paused || remaining % 2 == 0)
        {
            matrix->drawPixel(x + 15, y + 2, color);
            matrix->drawPixel(x + 15, y + 5, color);
        }
    }
    else
    {
        char label[6];
        snprintf(label, sizeof(label), "%03lu%02lu", static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
        int positions[5] = {5, 9, 13, 21, 25};
        for (int i = 0; i < 5; ++i)
            drawDigit3(matrix, label[i] - '0', x + positions[i], y + 1, color);
        if (state == ArcadeTimerState::Paused || remaining % 2 == 0)
        {
            matrix->drawPixel(x + 18, y + 2, color);
            matrix->drawPixel(x + 18, y + 4, color);
        }
    }
    uint32_t width = totalSeconds ? (remaining * 32 + totalSeconds - 1) / totalSeconds : 0;
    if (width > 32)
        width = 32;
    for (uint32_t i = 0; i < width; ++i)
        matrix->drawPixel(x + i, y + 7, color);
    if (remaining > 0 && remaining <= 10 && animationStyle == "playful_arcade")
    {
        uint32_t pulse = remaining % 2 ? 0xFF2D2D : 0xFFFFFF;
        matrix->drawPixel(x, y, pulse);
        matrix->drawPixel(x + 31, y, pulse);
    }
}

void ArcadeTimerManager::drawCompletion(FastLED_NeoMatrix *matrix, int16_t x, int16_t y, bool test)
{
    if (animationStyle == "playful_arcade")
    {
        for (int i = 0; i < 10; ++i)
            matrix->drawPixel(x + random(32), y + random(8), CHSV(random8(), 255, 180));
    }
    const uint8_t letters[4][5] = {
        {0x06, 0x05, 0x05, 0x05, 0x06}, {0x07, 0x05, 0x05, 0x05, 0x07},
        {0x05, 0x07, 0x07, 0x07, 0x05}, {0x07, 0x04, 0x06, 0x04, 0x07}};
    int16_t start = test ? x + 7 : x + 7;
    for (int letter = 0; letter < 4; ++letter)
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (letters[letter][row] & (1 << (2 - col)))
                    matrix->drawPixel(start + letter * 5 + col, y + 1 + row, static_cast<uint32_t>(0xFFFFFF));
}

void ArcadeTimerManager::drawFeedback(FastLED_NeoMatrix *matrix, int16_t x, int16_t y)
{
    uint32_t elapsed = millis() - feedbackStartedMs;
    if (feedback == Feedback::Cancel)
    {
        uint32_t divisor = feedbackDurationMs == 0 ? 1 : feedbackDurationMs;
        int width = static_cast<int>((elapsed * 32) / divisor);
        if (width > 32)
            width = 32;
        for (int col = 0; col < width; ++col)
            for (int row = 0; row < 8; ++row)
                matrix->drawPixel(x + col, y + row, static_cast<uint32_t>(0xFF2D2D));
        return;
    }
    if (feedback == Feedback::PlusOne || feedback == Feedback::MinusOne)
    {
        uint32_t color = feedback == Feedback::PlusOne ? 0x2ECC71 : 0xFF9F1C;
        drawTinySymbol(matrix, feedback == Feedback::PlusOne ? '+' : '-', x + 9, y + 1, color);
        drawDigit5(matrix, 1, x + 17, y, color);
        return;
    }
    if (feedback == Feedback::Start || feedback == Feedback::Resume)
    {
        uint32_t divisor = feedbackDurationMs == 0 ? 1 : feedbackDurationMs;
        int width = static_cast<int>((elapsed * 32) / divisor);
        if (width > 32)
            width = 32;
        for (int col = 0; col < width; ++col)
            for (int row = 0; row < 8; ++row)
                matrix->drawPixel(x + col, y + row, static_cast<uint32_t>(0x2ECC71));
    }
}

void ArcadeTimerManager::render(FastLED_NeoMatrix *matrix, int16_t x, int16_t y, bool idlePage)
{
    matrix->fillScreen(0);
    if (testAlarmActive)
        drawCompletion(matrix, x, y, true);
    else if (state == ArcadeTimerState::Ringing)
        drawCompletion(matrix, x, y, false);
    else if (state == ArcadeTimerState::Recovering)
    {
        // Compact SYNC indicator while waiting for NTP.
        for (int col = 4; col < 28; ++col)
            if ((col + millis() / 250) % 4 == 0)
                matrix->drawPixel(x + col, y + 3, static_cast<uint32_t>(0x3498DB));
    }
    else if (state == ArcadeTimerState::Idle || idlePage)
        drawIdle(matrix, x, y);
    else
        drawCountdown(matrix, x, y);
    if (feedback != Feedback::None)
        drawFeedback(matrix, x, y);
}

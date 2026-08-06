#ifndef ARCADE_TIMER_H
#define ARCADE_TIMER_H

#include <Arduino.h>
#include <FastLED_NeoMatrix.h>

enum class ArcadeTimerState : uint8_t
{
    Idle = 0,
    Running = 1,
    Paused = 2,
    Ringing = 3,
    Recovering = 4
};

class ArcadeTimerManager
{
public:
    void setup();
    void tick();
    void render(FastLED_NeoMatrix *matrix, int16_t x = 0, int16_t y = 0, bool idlePage = false);
    bool ownsDisplay() const;
    bool protectsDisplayPower() const;
    bool handleCenter();
    bool handleCenterLong();
    bool handleLeft();
    bool handleRight();
    void applyConfig(const char *json);
    void acknowledgeUsage(const char *json);
    void testAlarm(const char *json);
    void onMqttConnected();
    const char *stateName() const;

private:
    static constexpr size_t kUsageQueueCapacity = 128;

    struct UsageRecord
    {
        uint64_t producerId = 0;
        uint32_t sessionId = 0;
        int64_t startedAtEpoch = 0;
        int64_t endedAtEpoch = 0;
        uint32_t activeSeconds = 0;
        uint8_t outcome = 0;
        uint8_t timingQuality = 0;
    };

    enum class Feedback : uint8_t
    {
        None,
        Start,
        Resume,
        PlusOne,
        MinusOne,
        Reset
    };

    ArcadeTimerState state = ArcadeTimerState::Idle;
    Feedback feedback = Feedback::None;
    uint32_t feedbackStartedMs = 0;
    uint32_t feedbackDurationMs = 0;
    uint32_t defaultSeconds = 300;
    uint32_t totalSeconds = 300;
    uint32_t pausedSeconds = 300;
    int64_t deadlineUs = 0;
    time_t deadlineEpoch = 0;
    time_t expiredEpoch = 0;
    uint32_t configRevision = 0;
    uint32_t sequence = 0;
    uint32_t bootId = 0;
    uint32_t lastCenterMs = 0;
    bool centerCandidate = false;
    bool recoverable = false;
    bool alarmEnabled = true;
    bool alarmPlaying = false;
    bool restoreDisplayOff = false;
    uint32_t ringingEndsMs = 0;
    bool testAlarmActive = false;
    uint32_t testAlarmEndsMs = 0;
    String animationStyle = "playful_arcade";
    String melody = "arcade";
    uint64_t producerId = 0;
    uint32_t nextSessionId = 0;
    uint32_t currentSessionId = 0;
    int64_t sessionStartedEpoch = 0;
    uint64_t activeMillis = 0;
    int64_t activeSegmentStartedUs = 0;
    int64_t activeSegmentStartedEpoch = 0;
    bool sessionOpen = false;
    bool sessionTimingExact = true;
    UsageRecord usageQueue[kUsageQueueCapacity]{};
    uint16_t usageCount = 0;
    uint32_t droppedUsageRecords = 0;

    uint32_t remainingSeconds() const;
    bool timeValid() const;
    void start();
    void pause();
    void resume();
    void resetReady();
    void complete();
    void dismiss();
    void adjust(int32_t seconds);
    void setFeedback(Feedback next, uint32_t durationMs);
    void updateAlarm();
    void stopTestAlarm();
    void startActiveSegment();
    void stopActiveSegment(int64_t endedAtEpoch = 0);
    void finalizeSession(const char *outcome, int64_t endedAtEpoch = 0);
    void persist();
    void restore();
    void persistUsageQueue();
    void restoreUsageQueue();
    void enqueueUsage(const UsageRecord &record);
    void publishUsageRecord(const UsageRecord &record);
    void publishPendingUsage();
    void publishUsageStatus();
    String producerIdText() const;
    String usageSourceId(uint32_t sessionId) const;
    String usageTopic(uint32_t sessionId) const;
    void publishCapability();
    void publishState(bool retained);
    void publishConfigAck(const char *status, const char *error = nullptr);
    const char *melodyRtttl() const;
    void drawIdle(FastLED_NeoMatrix *matrix, int16_t x, int16_t y);
    void drawCountdown(FastLED_NeoMatrix *matrix, int16_t x, int16_t y);
    void drawCompletion(FastLED_NeoMatrix *matrix, int16_t x, int16_t y, bool test);
    void drawFeedback(FastLED_NeoMatrix *matrix, int16_t x, int16_t y);
};

extern ArcadeTimerManager ArcadeTimer;

#endif

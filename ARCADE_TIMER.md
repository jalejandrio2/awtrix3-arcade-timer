# TC001 native arcade timer

This public fork adds an on-device countdown timer to AWTRIX 3 `0.98`. It is
based on upstream commit `b8548eb4fdc8dac3fe40e9582177ec5c738530ba`
and retains the upstream CC BY-NC-SA 4.0 license.

## Device controls

| State | Left | Center | Right |
|---|---|---|---|
| Idle | Previous page | Start the configured timer from any page | Next page |
| Running | Minus one minute; finish if one minute or less remains | Pause; second press within 800 ms resets to the configured time and waits | Add one minute |
| Paused | Minus one minute; finish if one minute or less remains | Resume; second press within 800 ms resets to the configured time and waits | Add one minute |
| Ringing | No action | Stop the alarm and reset early | No action |

Button bounce below 120 ms is ignored. While active, the timer owns the display
and normal page rotation is suspended. The countdown uses the ESP32 monotonic
clock and does not depend on MQTT updates.

At expiry, the local buzzer and completion animation run for at most five
seconds. Center dismisses them early; otherwise the timer resets itself and
returns to normal rotation automatically.

When center starts the timer while the matrix is off, the firmware wakes the
matrix and protects it from screen-off commands for the active timer session.
After the alarm finishes or is dismissed, the matrix returns to off. Timers
started with the matrix already on leave it on after completion.

## MQTT contract

All topics are below the device's configured AWTRIX prefix:

- `timer/config` retained input: schema, revision, default minutes, animation
  style, melody, and alarm enabled state.
- `timer/command` non-retained input: a strictly validated `start_default`
  request. It accepts no duration; the device always uses its persisted default.
- `timer/test_alarm` input: starts a five-second local alarm test while idle.
- `timer/usage/ack` input: removes one committed usage record from the device
  queue by its `source_session_id`.
- `stats/timer/capability` retained output: device-authority handshake.
- `stats/timer` output: timer state, monotonic sequence, remaining time,
  recoverable epoch deadline, and whether the display must return to off.
- `stats/timer/config` retained output: applied/rejected configuration revision.
- `stats/timer/command` non-retained output: correlated `started`, `duplicate`,
  `busy`, or `rejected` acknowledgement.
- `stats/timer/usage/<producer_id>-<session_id>` retained output: one finalized
  session with UTC epochs, active seconds, outcome, and timing quality.
- `stats/timer/usage/status` retained output: queue depth, capacity, and dropped
  record count.

Timer state is published only for state/configuration transitions and MQTT
reconnects, never once per countdown second. Each physical start gets a stable
producer/session identifier. Only locally measured running time is recorded;
paused time and the completion alarm are excluded. Completed and cancelled
sessions are retained in a 128-record flash queue and resent after reconnect
until the admin commits and acknowledges them.

The timer persists state in ESP32 Preferences. A running deadline resumes after
power loss when network time becomes valid. Expirations no more than ten minutes
old ring on recovery; older expirations reset without a delayed alarm.

### Remote default start

Publish with QoS 1 and `retain=false`:

```json
{
  "schema": 1,
  "command_id": "a-unique-bounded-token",
  "action": "start_default",
  "source": "maximo_assist",
  "issued_at": 1786881600
}
```

The timer must be idle and device time must be valid. Commands older or more
than 60 seconds ahead are rejected, preventing a retained or delayed command
from starting a timer later. The last eight valid command IDs are persisted;
QoS retransmission therefore cannot create a second session after reboot.
Busy commands are consumed and acknowledged but never queued. `source` is audit
metadata only—authorization belongs at the MQTT broker and caller. Additional
fields, caller-provided durations, malformed tokens, and unsupported actions
are rejected.

## Safe flashing

`firmware.bin` is an application image for offset `0x10000`. Do not flash until
the exact device has two matching, complete 4 MiB backups. Verify the release
manifest SHA-256 and image size before writing. Upstream OTA is deliberately
disabled in this fork so the custom timer cannot be silently replaced.

# TC001 native arcade timer

This public fork adds an on-device countdown timer to AWTRIX 3 `0.98`. It is
based on upstream commit `b8548eb4fdc8dac3fe40e9582177ec5c738530ba`
and retains the upstream CC BY-NC-SA 4.0 license.

## Device controls

| State | Left | Center | Right |
|---|---|---|---|
| Idle | Previous page | Start the configured timer from any page | Next page |
| Running | Minus one minute; finish if one minute or less remains | Pause; second press within 800 ms cancels | Add one minute |
| Paused | Minus one minute; finish if one minute or less remains | Resume; second press within 800 ms cancels | Add one minute |
| Ringing | No action | Stop the alarm and reset | No action |

Button bounce below 120 ms is ignored. While active, the timer owns the display
and normal page rotation is suspended. The countdown uses the ESP32 monotonic
clock and does not depend on MQTT updates.

## MQTT contract

All topics are below the device's configured AWTRIX prefix:

- `timer/config` retained input: schema, revision, default minutes, animation
  style, melody, and alarm enabled state.
- `timer/test_alarm` input: starts a five-second local alarm test while idle.
- `stats/timer/capability` retained output: device-authority handshake.
- `stats/timer` output: timer state, monotonic sequence, remaining time, and
  recoverable epoch deadline.
- `stats/timer/config` retained output: applied/rejected configuration revision.

The timer persists state in ESP32 Preferences. A running deadline resumes after
power loss when network time becomes valid. Expirations no more than ten minutes
old ring on recovery; older expirations reset without a delayed alarm.

## Safe flashing

`firmware.bin` is an application image for offset `0x10000`. Do not flash until
the exact device has two matching, complete 4 MiB backups. Verify the release
manifest SHA-256 and image size before writing. Upstream OTA is deliberately
disabled in this fork so the custom timer cannot be silently replaced.

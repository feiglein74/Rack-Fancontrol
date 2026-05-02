# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESPHome firmware for a mini-rack fan controller on an ESP32-S3. Regulates two 12 V fans via PID against the CPU temperature of a MikroTik CRS305 (pulled live over SNMP). Intentionally over-engineered hobby build.

## Commands

All run from the repo root:

```bash
esphome config   rack-fancontrol.yaml                  # validate YAML
esphome compile  rack-fancontrol.yaml                  # build only
esphome run      --device <ip|serial> rack-fancontrol.yaml  # build + upload + logs
esphome logs     --device <ip> rack-fancontrol.yaml    # stream logs via API
```

- ESPHome 2026.3.x, framework `esp-idf`.
- Device currently at `10.10.1.100` (xXx-IoT VLAN); serial fallback on `/dev/ttyACM0` @ 115200 when plugged in.
- OTA port 3232, API port 6053, web UI port 80.

## Hardware pin map

| GPIO  | Function                                  |
|-------|-------------------------------------------|
| 4     | Fan 1 (4-pin) PWM output, LEDC @ 25 kHz   |
| 5     | Fan 1 (4-pin) tach input, pulled up, 2 pulses/rev → ÷2 filter |
| 6     | Fan 2 (2-pin) PWM into 60N03 MOSFET module, LEDC @ **100 Hz** |
| 8 / 9 | I²C SDA / SCL to INA219 (addr 0x40, 0.1 Ω shunt, **100 kHz** bus) |

The MOSFET module is pre-built (resistors, flyback diode onboard). ESP powered from 12 V via a buck converter into the 5V pin (onboard AMS1117 makes 3.3 V). INA219 must be powered from 3.3 V, not 5 V — its I²C pull-ups go to VCC and would clamp SDA/SCL to 5 V into the 3.3 V ESP GPIOs.

## Why some of the unusual settings

- **I²C at 100 kHz, not the 50 kHz default.** At 50 kHz the cheap INA219 breakout didn't ACK; at 100 kHz it does. Don't lower this.
- **Fan 2 PWM at 100 Hz.** 2-pin BLDC fans can't speed-control at 25 kHz supply chopping — they only cut through on/off. Low-frequency PWM lets the motor inductance average the pulses. Audible hum is the tradeoff. A smoothing electrolytic (47–100 µF across the fan) is the silent upgrade.
- **`on_boot` sets both fans to 100 %.** Fail-safe so the rack doesn't cook if PID or WiFi or SNMP are slow to come up. PID in COOL mode takes over and writes over this.
- **EMA filter (α=0.2, poll 5 s) on the MikroTik temp.** RouterOS reports only whole-degree values — the `.11.0` OID's deciDegrees encoding is cosmetic (always ends in `0`). EMA smooths the quantization so the PID's D-term doesn't see step impulses.

## Custom SNMP component

`components/snmp_sensor/` is a minimal ESPHome external_component — a single-OID SNMPv2c GET client over UDP (BSD sockets, non-blocking state machine in `loop()`). Hand-rolled ASN.1/BER encoder and value parser; no external library. Parses only INTEGER responses (1–4 bytes, signed). Request is 47 bytes, response typically 46–47 bytes for the MikroTik OIDs.

Config:
```yaml
- platform: snmp_sensor
  host: 10.0.0.65
  community: public
  oid: "1.3.6.1.4.1.14988.1.1.3.11.0"
  update_interval: 5s
```

Depends on the ESP being able to route to the target — the ESP lives on the IoT VLAN (10.10.1.0/24) and the MikroTik on mgmt (10.0.0.0/24); the firewall rule allowing UDP/161 must stay in place.

## Control architecture

```
MikroTik SNMP → mikrotik_cpu_temp (EMA-filtered)
                         │
                         ▼
                  climate.pid (Rack Cooling)
                         │ cool_output (0.0..1.0)
                         ▼
                  template float output (fan_group_output)
                         │ fans out same level to both
                ┌────────┴────────┐
                ▼                 ▼
          fan1_pwm_out      fan2_pwm_out   (LEDC channels)
                │                 │
                ▼                 ▼
           GPIO4 → 4-pin    GPIO6 → MOSFET → 2-pin
```

`fan.speed` entities (`fan1`, `fan2`) wrap the same LEDC outputs for manual override when `climate.mode = OFF`. The template output routes through `fan.turn_on`/`fan.turn_off` so the entity state tracks the PID's commands live (sliders in the UI follow the regulator).

**Staged cooling:** the template output spreads a single PID value across the two fans non-linearly — Fan 1 (the quiet 4-pin) takes 0..100 % across PID 0..0.5, then Fan 2 (the louder 2-pin MOSFET line) ramps 0..100 % across PID 0.5..1.0. Rationale: cruise silently on the quiet fan, only wake the loud one when one fan can't keep up. Side effect: effective PID gain is ~2× in the low-output range (only fan 1 active) vs. the high range (both), so kp values tuned for the old linear mapping may feel too aggressive.

## Tuning / diagnostics

- Live-tune PID via HA service: `climate.pid.set_control_parameters` (kp/ki/kd) — no reflash.
- Step-response autotune: `climate.pid.autotune` — runs a minutes-long excitation cycle.
- `Uptime` + `Reset Reason` sensors are on the web UI; watch them if the device looks unstable.
- **Do not reset the ESP via DTR/RTS immediately after an OTA** — safe_mode requires 60 s of uptime to commit the new image, an early reset triggers rollback to the previous firmware.

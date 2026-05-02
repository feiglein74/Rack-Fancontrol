# Rack-Fancontrol

An over-engineered hobby fan controller for a mini server rack.

An **ESP32-S3** runs ESPHome firmware that pulls live CPU + SFP+ DDM
temperatures off a **MikroTik CRS305** over SNMP, feeds them into a
PID regulator, and drives two 12 V fans with a staged-cooling profile:
the quiet 4-pin fan handles the low load, the loud 2-pin fan only
wakes up when one fan can't keep up.

Goal: keep the rack cool, keep the office quiet, and have something
fun to print, solder, and tune.

---

## Hardware

| Part | Role |
|------|------|
| ESP32-S3-DevKitC-1 | Brain. Runs ESPHome 2026.3.x on `esp-idf`. |
| 4-pin PC fan (12 V) | Primary fan. PWM @ 25 kHz on GPIO4, tach on GPIO5. |
| 2-pin BLDC fan (12 V) | Boost fan. Driven via 60N03 MOSFET module @ 100 Hz on GPIO6. |
| INA219 (0.1 Ω shunt) | Live current/power monitoring of the fan rail. |
| XW026FR4 buck converter | 12 V → 5 V for the ESP. |
| BSS138 level shifter | 3.3 V ↔ 5 V on the PWM/tach lines for the 4-pin fan. |
| 3× WAGO 221-415 | Solderless terminal strips for 12 V / 12 V-after-INA / GND. |

I²C runs at **100 kHz** (the cheap INA219 breakout doesn't ACK at the
50 kHz default). Fan 2 PWM runs at **100 Hz** because 2-pin BLDC fans
can't speed-control at 25 kHz supply chopping — they only cut through
on/off; low-frequency PWM lets motor inductance average the pulses.

## Control architecture

```
MikroTik SNMP (CPU + SFP+1/2 temps, 60 s poll, EMA-filtered)
                         │
                         ▼
                  rack_max_temp (template max)
                         │
                         ▼
                  climate.pid (45 °C target, ±1.5 °C deadband)
                         │ cool_output (0.0..1.0)
                         ▼
                  staged template output
                ┌────────┴────────┐
                ▼                 ▼
          fan1 (0..0.5)     fan2 (0.5..1.0)
        4-pin LEDC PWM    2-pin via MOSFET
```

`fan.speed` entities also wrap each LEDC output, so when
`climate.mode = OFF` the fans become manually controllable from
Home Assistant (or the local web UI) without the PID overwriting.

## Features

- **PID over SNMP** — the regulator runs against a hand-rolled
  SNMPv2c GET client (`components/snmp_sensor/`, ~250 lines of C++,
  no external library — just an ASN.1/BER encoder, a value parser
  for INTEGER + Gauge32, and a non-blocking UDP state machine).
- **Staged cooling** — a single PID output is split non-linearly
  across two fans so the loud one stays off until really needed.
- **Auto-calibration** — a "Fan 1 kalibrieren" button sweeps PWM
  duty 0/25/40/55/70/85/100 % and interpolates the dead-zone
  threshold from the tach response. The PID output is then mapped
  onto `[deadzone..100 %]` so the fan never sits in its lazy band.
- **Live tuning** — `climate.pid.set_control_parameters` and
  `climate.pid.autotune` are exposed; no reflash needed to retune.
- **Local INA219 telemetry** — fan-rail current, voltage, and power
  sampled at 1 Hz.
- **Home Assistant native** — exposes climate, fan, sensor, and text
  entities via the ESPHome API; OTA on 3232, web UI on 80.

## Build & flash

ESPHome 2026.3.x is required. WiFi credentials live in `secrets.yaml`
(gitignored — copy `secrets.yaml.example` if present, or create your
own with `wifi_ssid` / `wifi_password`).

```bash
esphome config   rack-fancontrol.yaml                   # validate
esphome compile  rack-fancontrol.yaml                   # build
esphome run      --device <ip|serial> rack-fancontrol.yaml
esphome logs     --device <ip> rack-fancontrol.yaml
```

Initial flash via USB-C; subsequent flashes go OTA. Don't power-cycle
the device within 60 s of an OTA — `safe_mode` will roll the image
back as a "boot loop" otherwise.

## Repo layout

```
rack-fancontrol.yaml          ESPHome top-level config
components/snmp_sensor/       Custom external_component (SNMPv2c GET client)
enclosure/                    OpenSCAD source for the 3D-printed case
CLAUDE.md                     Engineering notes, pin map, why-things-are
```

## Enclosure (in progress)

The controller is migrating from a Dupont-jungle prototype into a
parametric 3D-printed case, designed in **OpenSCAD** for FDM printing.

**Current state — base plate (`enclosure/base_plate.scad`):**

- 160 × 90 × 3 mm rounded rectangle, ventilation slots underneath
  the warm modules (ESP, MOSFET) for passive convection.
- **Snap cradles** for every module — no screws. Four printed
  spring-clips per module catch the PCB edge with a 0.8 mm lip;
  PCBs slide in from above and are held by interference fit.
- The ESP DevKit is mounted **flipped** (header pins facing up so
  Dupont jumpers plug straight in from above) on 8 mm standoffs to
  clear the WiFi antenna and bottom-side components.
- WAGO 221-415 holders use a community STL
  ([Wago 221-415 Connector x 5 Mount](https://www.printables.com/model/321904)
  by *Wiseone*), imported and re-anchored from the parametric layout.
- All module dimensions are typical values; after the first test
  print they get fine-tuned with calipers per real component.

**Planned next:**

- Walls + lid (snap-on), with cable-entry cutouts on the short sides
  (12 V in, fan tails out, USB-C service hatch).
- Either rack-format (10″) front panel or free-standing — to be
  decided depending on whether the LabRax has a slot to spare.
- A small front cutout for an OLED status display is on the table.

The OpenSCAD source is intentionally parameter-heavy so any module
swap (different INA breakout, smaller buck, etc.) is a one-line edit
plus a re-render.

## Known quirks

- **Fan 2 hum.** 100 Hz PWM is audible. A 47–100 µF electrolytic
  across the fan smooths it almost completely — silent upgrade,
  not yet built in.
- **MikroTik temp resolution.** RouterOS reports CPU temperature as
  whole degrees only (the deciDegree OID encoding is cosmetic). The
  EMA filter (α=0.4, 60 s poll) smooths the quantization steps so
  the PID's D-term doesn't see the integer jumps as kicks.
- **SFP+ DDM.** Module temperatures arrive as Gauge32 over SNMP.
  The custom parser was extended to handle `0x42` after the initial
  INTEGER-only version returned NaN. The `sfpplus4-uplink` module
  is excluded from the PID input — its DDM reading is too jumpy.
- **VLAN routing required.** The ESP lives on an IoT VLAN; the
  MikroTik on management. UDP/161 must be allowed from one to the
  other.

## License

Personal hobby project; published in case any of it is useful. No
warranty implied — this thing is happily blasting fans in a closet,
not running a datacenter.

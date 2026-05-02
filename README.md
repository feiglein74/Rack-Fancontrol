# Rack-Fancontrol

Eine bewusst überdimensionierte Lüftersteuerung für ein Mini-Serverrack.

Ein **ESP32-S3** läuft mit ESPHome-Firmware, holt sich live die CPU-
und SFP+-DDM-Temperaturen eines **MikroTik CRS305** per SNMP, schickt
sie durch einen PID-Regler und steuert damit zwei 12-V-Lüfter mit
einer gestaffelten Kühlkurve: Der leise 4-Pin-Lüfter macht die
Grundlast, der laute 2-Pin-Lüfter wird erst zugeschaltet, wenn ein
Lüfter allein nicht mehr reicht.

Ziel: Rack kühl halten, Büro leise halten — und dabei etwas zum
Drucken, Löten und Tunen haben.

---

## Hardware

| Bauteil | Funktion |
|---------|----------|
| ESP32-S3-DevKitC-1 | Hirn. Läuft ESPHome 2026.3.x auf `esp-idf`. |
| 4-Pin-PC-Lüfter (12 V) | Hauptlüfter. PWM @ 25 kHz auf GPIO4, Tacho auf GPIO5. |
| 2-Pin-BLDC-Lüfter (12 V) | Boost-Lüfter. Über 60N03-MOSFET-Modul @ 100 Hz auf GPIO6. |
| INA219 (0,1 Ω Shunt) | Live-Strom-/Leistungsmessung der Lüfterschiene. |
| XW026FR4 Buck Converter | 12 V → 5 V für den ESP. |
| BSS138 Levelshifter | 3,3 V ↔ 5 V auf den PWM-/Tacho-Leitungen für den 4-Pin-Lüfter. |
| 3× WAGO 221-415 | Lötfreie Klemmen für 12 V / 12 V-nach-INA / GND. |

I²C läuft mit **100 kHz** (das billige INA219-Breakout antwortet bei
den 50 kHz Default nicht). Lüfter 2 PWM läuft mit **100 Hz**, weil
2-Pin-BLDC-Lüfter sich bei 25 kHz Versorgungschoppen nicht regeln
lassen — sie schalten dann nur an/aus durch. Bei niedriger PWM-
Frequenz mittelt die Motor-Induktivität die Pulse weg.

## Regelarchitektur

```
MikroTik SNMP (CPU + SFP+1/2 Temperaturen, 60 s Poll, EMA-gefiltert)
                         │
                         ▼
                  rack_max_temp (Template-Maximum)
                         │
                         ▼
                  climate.pid (45 °C Sollwert, ±1,5 °C Totband)
                         │ cool_output (0,0..1,0)
                         ▼
                  Staged-Cooling Template-Output
                ┌────────┴────────┐
                ▼                 ▼
        Lüfter 1 (0..0,5)  Lüfter 2 (0,5..1,0)
        4-Pin LEDC PWM     2-Pin via MOSFET
```

Die `fan.speed`-Entities umschließen zusätzlich jeden LEDC-Output —
wenn `climate.mode = OFF` werden die Lüfter manuell aus Home Assistant
oder dem lokalen Web-UI steuerbar, ohne dass der PID-Regler dazwischen-
funkt.

## Features

- **PID über SNMP** — der Regler arbeitet gegen einen selbst-
  geschriebenen SNMPv2c-GET-Client (`components/snmp_sensor/`,
  ~250 Zeilen C++, ohne externe Library — nur ein ASN.1/BER-Encoder,
  ein Wert-Parser für INTEGER + Gauge32 und ein nicht-blockierender
  UDP-State-Machine).
- **Gestaffelte Kühlung** — ein einzelner PID-Output wird nicht-linear
  auf zwei Lüfter aufgeteilt, damit der Laute aus bleibt, solange es
  geht.
- **Auto-Kalibrierung** — ein Button "Fan 1 kalibrieren" fährt die
  PWM-Pegel 0/25/40/55/70/85/100 % ab und interpoliert die Totzonen-
  Schwelle aus der Tacho-Antwort. Der PID-Output wird anschließend
  auf `[Totzone..100 %]` skaliert, damit der Lüfter nie in seinem
  trägen Bereich hängt.
- **Live-Tuning** — `climate.pid.set_control_parameters` und
  `climate.pid.autotune` sind als Service exponiert; kein Reflash
  nötig.
- **Lokale INA219-Telemetrie** — Strom, Spannung und Leistung der
  Lüfterschiene mit 1 Hz.
- **Home-Assistant-nativ** — Climate-, Fan-, Sensor- und
  Text-Entities über die ESPHome-API; OTA auf Port 3232,
  Web-UI auf Port 80.

## Build & Flash

ESPHome 2026.3.x ist Voraussetzung. WLAN-Zugangsdaten liegen in
`secrets.yaml` (gitignored — selbst anlegen mit `wifi_ssid` /
`wifi_password`).

```bash
esphome config   rack-fancontrol.yaml                   # validieren
esphome compile  rack-fancontrol.yaml                   # bauen
esphome run      --device <ip|serial> rack-fancontrol.yaml
esphome logs     --device <ip> rack-fancontrol.yaml
```

Erstflash über USB-C, danach geht alles per OTA. Innerhalb 60 s
nach einem OTA das Gerät **nicht** stromlos machen — sonst rollt
`safe_mode` das Image als vermeintlicher Boot-Loop wieder zurück.

## Repo-Struktur

```
rack-fancontrol.yaml          ESPHome-Hauptkonfiguration
components/snmp_sensor/       Eigene external_component (SNMPv2c-GET-Client)
enclosure/                    OpenSCAD-Quellen für das 3D-Druck-Gehäuse
CLAUDE.md                     Engineering-Notizen, Pin-Map, Begründungen
```

## Gehäuse (work in progress)

Die Steuerung wandert gerade vom Dupont-Kabel-Dschungel in ein
parametrisches 3D-Druck-Gehäuse, entworfen in **OpenSCAD** für
FDM-Druck.

**Aktueller Stand — Bodenplatte (`enclosure/base_plate.scad`):**

- 160 × 90 × 3 mm abgerundetes Rechteck, mit Lüftungsschlitzen
  unter den warmen Modulen (ESP, MOSFET) für passive Konvektion.
- **Snap-Cradles** für jedes Modul — keine Schrauben. Vier
  gedruckte Federclips pro Modul greifen mit 0,8 mm Lippe über
  die PCB-Kante; die Platinen werden von oben eingeklippst.
- Das ESP-DevKit ist **kopfüber** montiert (Pin-Header oben,
  damit Dupont-Stecker direkt von oben aufgesteckt werden können)
  auf 8 mm Standoffs, damit die WiFi-Antenne und die Unterseiten-
  Bauteile Platz haben.
- WAGO-221-415-Halter nutzen ein Community-STL
  ([Wago 221-415 Connector x 5 Mount](https://www.printables.com/model/321904)
  von *Wiseone*), importiert und im parametrischen Layout neu
  positioniert.
- Alle Modul-Maße sind erst mal Typenwerte; nach dem ersten
  Testdruck werden sie pro echtem Bauteil mit Messschieber
  feingetrimmt.

**Geplant:**

- Wände + Deckel (zum Aufstecken), mit Kabelausgängen auf den
  schmalen Seiten (12 V rein, Lüfteranschlüsse raus, USB-C-
  Wartungsklappe).
- Entweder Rack-Format (10″) als Frontblende oder freistehend —
  je nachdem, ob im LabRax noch ein Slot frei wird.
- Eine kleine Frontaussparung für ein OLED-Statusdisplay liegt
  noch auf dem Tisch.

Die OpenSCAD-Quelle ist absichtlich parameterlastig: Modul tauschen
(anderes INA-Breakout, kleinerer Buck etc.) ist eine Zeile Edit
plus Re-Render.

## Bekannte Eigenheiten

- **Lüfter-2-Brumm.** 100 Hz PWM hört man. Ein 47–100 µF Elko
  parallel zum Lüfter glättet das fast komplett weg — silent
  upgrade, noch nicht eingebaut.
- **MikroTik-Temperaturauflösung.** RouterOS meldet die CPU-
  Temperatur nur in ganzen Grad (die deciDegree-OID-Codierung
  ist kosmetisch). Der EMA-Filter (α=0,4, 60 s Poll) glättet
  die Quantisierungsstufen, damit der D-Term des PID die
  Integer-Sprünge nicht als Schläge sieht.
- **SFP+-DDM.** Modultemperaturen kommen als Gauge32 über SNMP.
  Der Custom-Parser musste auf Tag `0x42` erweitert werden, weil
  die ursprüngliche INTEGER-only-Version NaN zurückgab. Das
  Modul `sfpplus4-uplink` ist aus dem PID-Input ausgenommen —
  dessen DDM-Werte schwanken zu stark.
- **VLAN-Routing nötig.** Der ESP hängt im IoT-VLAN, der MikroTik
  im Management-VLAN. UDP/161 muss zwischen beiden geöffnet sein.

## Lizenz

Privates Hobbyprojekt; veröffentlicht, falls jemand etwas davon
brauchen kann. Keine Garantie — das Ding pustet glücklich Lüfter
in einem Schrank, kein Rechenzentrum dahinter.

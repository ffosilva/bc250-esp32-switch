# Pinout & wiring (ESP32-WROOM / DevKitC)

Wiring reference for the BC250 power switch on an **ESP32-DevKitC (38-pin, ESP32-WROOM-32)**.
Pin numbers live in [include/board.h](../include/board.h) — change them there if you
rewire.

## GPIO assignment

| Signal | GPIO | DevKitC header | Connects to | Notes |
|--------|------|----------------|-------------|-------|
| `BUTTON_SENSE` | **25** | left | Momentary switch, terminal A | Internal pull-up; pressed reads LOW |
| `BUTTON_GND` | **26** | left | Momentary switch, terminal B | Driven LOW as the switch's ground |
| `PS_ON_PIN` | **32** | left | ATX `PS_ON#` (green wire) | **Open-drain**, active LOW: LOW = PSU on, released = off |
| `BOARD_SENSE` | **34** | left | BC250 `TPMS1` pin 9 | ~3.3 V when the board is up, 0 when off. **ADC1**, input-only |
| `5V` / `VIN` | — | left | PSU `+5VSB` | Permanent power for the ESP32 |
| `GND` | — | either | PSU GND + board GND | Common ground |

Why these pins:

- **GPIO34 is ADC1** (ADC1_CH6). ADC2 can't be read while WiFi is running (setup
  portal), so the analog board-sense must be on ADC1. GPIO34 is input-only, which is
  all it needs.
- **GPIO32** is high-impedance at reset, so `PS_ON#` stays released (PSU off) while
  the ESP32 boots. It is not a strapping pin.
- GPIO25/26 are plain GPIOs with no boot-time function.
- Avoided on purpose: strapping pins (GPIO0, 2, 5, 12, 15) and the flash pins
  (GPIO6–11).

## DevKitC header (left column, USB at the bottom)

All four signals and power are on the **left** header (the side with `EN`, `VP`, `VN`):

```
   ┌────────────────────────────┐
   │ 3V3                        │
   │ EN                         │
   │ IO36 (VP)                  │
   │ IO39 (VN)                  │
   │ IO34   ◄── TPMS1 pin 9     │  ADC1, input-only
   │ IO35                       │
   │ IO32   ──► PS_ON#          │  open-drain, active LOW
   │ IO33                       │
   │ IO25   ◄── switch A        │  pull-up
   │ IO26   ──► switch B        │  driven LOW
   │ IO27                       │
   │ IO14                       │
   │ IO12   (strapping, avoid)  │
   │ GND    ◄── common ground   │
   │ IO13                       │
   │ SD2 / SD3 / CMD  (flash)   │  leave unconnected
   │ 5V     ◄── PSU +5VSB       │
   └────────────────────────────┘
```

> Header order can differ slightly between DevKitC clones. **Go by the silkscreen labels**
> (`IO25`, `IO26`, `IO32`, `IO34`, `5V`, `GND`), not the physical position.

## Connections

```
 PSU +5VSB ────────────────────────► ESP32 5V / VIN
 PSU GND ──────────────────────────► ESP32 GND  (common ground, also to the BC250)

 Switch terminal A ────────────────► GPIO25   (internal pull-up)
 Switch terminal B ────────────────► GPIO26   (driven LOW = local ground)

 PSU PS_ON# (green) ──[optional buffer, see below]── GPIO32  (open-drain, active LOW)

 BC250 TPMS1 pin 9 ────────────────► GPIO34   (analog, ADC1)
```

### `PS_ON#` 5 V caution (recommended buffer)

`PS_ON#` idles at ~5 V, pulled up **inside the PSU**. Open-drain firmware means the
ESP32 never *drives* 5 V, but the pin still *sees* 5 V whenever it's released, and
that is above the ESP32 GPIO absolute maximum (VDD + 0.3 V ≈ 3.6 V). It tends to work,
but it stresses the pad over time. Recommended fix — use a small low-side switch so the
ESP32 only ever drives a 3.3 V gate:

```
                                  PSU PS_ON# (green)
                                         │
                                         ├───────────  (PSU pull-up to ~5V is internal)
                                         │
                                       ┌─┴─┐
                    1 kΩ                │ D │   2N7000 / BSS138 N-MOSFET
 GPIO32 ──[ 1 kΩ ]──────────────────── │ G │   (or any small NPN: 1 kΩ base resistor,
                                        │ S │    emitter to GND)
                                       └─┬─┘
                                         │
                                        GND

 + 100 kΩ from gate to GND so PS_ON# stays released while the ESP32 boots.
```

The buffer **inverts** the signal: GPIO32 HIGH turns the MOSFET on and pulls `PS_ON#`
LOW (PSU on). If you add it, flip the logic levels in
[include/board.h](../include/board.h):

```cpp
const int PS_ON_ASSERT  = HIGH;  // MOSFET on  -> PS_ON# pulled LOW -> PSU on
const int PS_ON_RELEASE = LOW;   // MOSFET off -> PSU pull-up wins  -> PSU off
```

and change `pinMode(PS_ON_PIN, OUTPUT_OPEN_DRAIN)` to `OUTPUT` in
[src/main.cpp](../src/main.cpp) and [src/portal.cpp](../src/portal.cpp). If you skip the
buffer, at minimum put a ~1 kΩ series resistor between `PS_ON#` and GPIO32 to limit
the clamp-diode current.

## Connector pinouts

**ATX 24-pin main connector** — tap three pins:

```
               +3.3V ─┤  1 │ 13 ├─ +3.3V
               +3.3V ─┤  2 │ 14 ├─ −12V
                 GND ─┤  3 │ 15 ├─ GND
                 +5V ─┤  4 │ 16 ├─ PS_ON#   ◄── GPIO32 (green, open-drain, active LOW)
                 GND ─┤  5 │ 17 ├─ GND      ◄── ESP GND (any GND pin works)
                 +5V ─┤  6 │ 18 ├─ GND
                 GND ─┤  7 │ 19 ├─ GND
              PWR_OK ─┤  8 │ 20 ├─ (RSVD)
ESP 5V/VIN ◄── +5VSB ─┤  9 │ 21 ├─ +5V
                +12V ─┤ 10 │ 22 ├─ +5V
                +12V ─┤ 11 │ 23 ├─ +5V
               +3.3V ─┤ 12 │ 24 ├─ GND
```

**TPMS1 header** — single pin for board-power sense:

```
   PCICLK ─┤  1   2 ├─ GND
    FRAME ─┤  3   4 ├─ SMB_CLK_MAIN
  PCIRST# ─┤  5   6 ├─ SMB_DATA_MAIN
     LAD3 ─┤  7   8 ├─ LAD2
       3V ─┤  9  10 ├─ LAD1      ◄── pin 9 (3V) = board-on sense ──► GPIO34
     LAD0 ─┤ 11  12 ├─ GND
          ─┤     14 ├─ S_PWRDWN#
     3VSB ─┤ 15  16 ├─ SERIRQ#
      GND ─┤ 17  18 ├─ GND
```

Pin 9 is the only TPMS1 pin used: it reads ~3.3 V when the board is powered and 0 V
when off. No ground wire is needed from this header — the ESP32 already shares ground
with the board through the ATX connector.

`TPMS1` is a higher-impedance signal that hovers near the logic threshold, so it is read
as an **analog** voltage (16× oversampled, with hysteresis) rather than a digital pin.

## Powering and flashing

- Power the ESP32 from `+5VSB` into the `5V` (VIN) pin so it runs whether the machine is
  on or off. Don't connect USB and `+5VSB` at the same time without a diode/ideal-diode
  between them, or the two 5 V sources will fight.
- The DevKitC's USB-UART bridge (CP210x/CH340) is used for flashing and the serial log
  (115200 baud). No `BOOT`/`EN` button handling is needed on most boards; if upload
  fails to connect, hold `BOOT` while it says `Connecting...`.

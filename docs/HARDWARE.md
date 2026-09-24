# Hardware — verified, do not re-derive

Everything here was read off the attached board or its schematic. It is measured fact,
not inference: **do not re-derive it, and do not "correct" it from a reseller listing
or the wiki of the board one digit away.** Routed here from AGENTS.md §2.

**Board: Waveshare ESP32-S3-Touch-LCD-4B** ("Smart 86 Box"). Confirmed via the official
BSP and repo; the non-"B" `ESP32-S3-Touch-LCD-4` is a *different, industrial* board whose
wiki pin table is **wrong for this one**. Do not follow it.

Read live from the attached board with `esptool`:

```
Chip type:  ESP32-S3 (QFN56) revision v0.2
Features:   WiFi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Flash:      16MB, quad (4 data lines), 3.3V
MAC:        44:1b:f6:89:95:dc
USB mode:   USB-Serial/JTAG
```

Module is `ESP32-S3-WROOM-1-N16R8`. PSRAM is **8 MB octal**; flash is 16 MB quad.

| Part | Detail |
|---|---|
| Display | 4" IPS 480×480, **ST7701** controller, **16-bit parallel RGB565** |
| LCD init bus | 3-wire SPI **through the TCA9554 IO expander** — costs no ESP32 GPIO |
| Pixel clock | 16 MHz (BSP) → ~60 Hz panel refresh |
| Framebuffer | 480×480×2 = **450 KiB**, in PSRAM |
| Touch | **GT911**, 5-point, I²C `0x5D`, **polled — INT is behind the expander** |
| I²C bus | single shared bus, **GPIO47 SDA / GPIO48 SCL**, 400 kHz |
| I²C devices | TCA9554 `0x20`, ES8311 `0x18`, AXP2101 `0x34`, ES7210 `0x40`, PCF85063 `0x51`, GT911 `0x5D`, QMI8658 `0x6B` |
| Audio | ES8311 codec + ES7210 ADC + NS4150B amp (2 W), 2× MEMS mics |
| Other | AXP2101 PMIC, PCF85063 RTC, QMI8658 6-axis IMU |
| Backlight | GPIO4, LEDC PWM |
| Enclosure | 86.5 × 86.5 × 14 mm, standard 86-type wall plate, case included |

**Not on this board** (reseller listings get this wrong): no relays, no mains input, no
PoE, **no microSD**, no buzzer, no RGB LED, no CAN/RS485/Ethernet.

**Power:** 2× USB-C, or PH2.0 Li-ion, or `5V_IN` on the rear header. **USB-C is the supply**
— the side-edge port placement is fine for a desk unit and the rear header is not needed.
(It would only matter for a flush wall install, where the side ports become unreachable.)

**A PH2.0 cell is supported as a UPS** since D61, and these are schematic facts, not
assumptions:

- **AXP2101 DCDC1 (pins 23/22/21) is `VCC_3V3`**, which feeds the ESP32-S3, the panel, the
  GT911 *and the AP3032 backlight boost*. VSYS switches between VBUS and BAT by itself, so
  unplugging USB interrupts nothing and the backlight stays lit. It is a power path, not a
  changeover switch.
- **J1 is the battery header: pin 1 GND, pin 2 VBAT1**, silkscreened `+`/`-`. Cell vendors
  are not consistent about which pin gets the red wire. **Meter it.** Reversed is a dead
  PMIC.
- **The back cover has a cutout over that socket**, so a cell plugs in without opening the
  case — which is just as well, because at 14 mm total depth nothing fits inside it. **The
  cell is stuck to the back of the case** (owner's call, D61 amendment): double-sided foam
  tape, never cyanoacrylate on the pouch, no clamping or folding, and leave slack in the
  lead so the plug is not what holds the cell on. Low on the back rather than centred — a
  10 mm block at the bottom edge leans the panel back a few degrees instead of making it
  rock.
- **The PMIC's IRQ pin does not reach an ESP32 GPIO** (pull-up, no second occurrence in the
  schematic), so the battery is polled, like the GT911.
- **A cold start on battery alone needs a PWRKEY press** — datasheet §6.5.2, the BATFET is
  off until the key is pressed or an adapter appears. Unplugging a *running* device is
  seamless.
- Draw is **1.2–1.9 W calculated** (39 mA through the backlight string: 200 mV over R30's
  5.1 Ω into the AP3032), so roughly four hours from 2000 mAh. **Calculated, not measured**
  — the firmware logs the discharge once a minute so the first unplugging settles it.

**GPIO budget is effectively zero.** The RGB bus consumes nearly everything. Expansion
goes over I²C, or by repurposing TCA9554 EXIO pins after init.

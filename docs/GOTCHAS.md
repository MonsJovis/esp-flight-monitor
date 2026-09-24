# Gotchas that will cost you a day

Each of these was found the expensive way on this hardware. They are not general
embedded advice; every one is specific to this board, this panel or these APIs.
Routed here from AGENTS.md §7.

**Firmware / display**
- **Flash writes tear the display — MEASURED, and they do not.** espressif/esp-bsp#570 is
  real on this silicon+panel combo, but it does not reproduce in this configuration.
  Measured: an NVS commit costs **3 µs** against a 49.7 ms worst-case frame gap, and
  sustained commits under a high-contrast moving pattern produce **no visible tearing**.
  Two framebuffers (AGENTS.md §8, PLAN.md M1) are the likely reason.
  **Do NOT pause LVGL around NVS writes.** Holding the display lock across a commit burst
  stalls rendering for ~2.85 s to save 3 µs — the mitigation is far worse than the disease.
  Keep `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and `CONFIG_SPIRAM_RODATA=y` on. Full numbers
  in docs/DECISIONS.md D29.
- **A screenshot reads frame buffer 0, which is not necessarily what is on the glass.**
  `esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb)` hands back the FIRST buffer and this
  build has two, so after a single redraw buffer 0 still holds the frame BEFORE the change.
  Every grab of a screen that had just been changed and then gone still was one state out
  of date, and said nothing about it — the image is a valid picture of the wrong moment.
  It only bites a STATIC screen: anything animating redraws continuously and both buffers
  converge, which is why the M11 loading states photographed correctly while the no-route
  fixture beside them came back twice showing the state before it. `dbg_screen.c` now
  invalidates the whole screen once per buffer before capturing. If you add a third
  buffer, that loop already follows `CONFIG_BSP_LCD_RGB_BUFFER_NUMS`.
- **Stay at 80 MHz PSRAM.** 120 MHz is experimental and temperature-sensitive — a real
  risk for an always-on panel behind glass.
- **WiFi bursts compete for PSRAM bandwidth** and cause visible drift. This is the #1
  field failure mode for RGB panels. Keep network work off the render path.
- **Strapping pins are on the RGB bus**: GPIO3 (VSYNC), GPIO45 (G3), GPIO46 (HSYNC), and
  GPIO0 is BOOT. Do not back-drive these during reset.
- Anti-tearing is **off** in the BSP default (`BSP_LCD_RGB_BUFFER_NUMS=1`); **we set 2**,
  and that is measured, not assumed. Two is both *faster* than one (28.5 vs 21.4 FPS) and
  tear-free; three buys nothing (D12, PLAN M1).
- **Large fonts land in PSRAM, not flash.** `CONFIG_SPIRAM_RODATA=y` — mandated at the top of
  this block as the tearing mitigation — relocates `.rodata`, and LVGL fonts *are* `.rodata`.
  So the 100 px hero face and its shrink ladder compete with the framebuffer for PSRAM
  bandwidth: the project's #1 risk and its most distinctive design choice pulling on the
  same bus. **Measured in M1, and it did not materialise:** at two framebuffers the 100 px
  face costs **zero** FPS; at one it costs 6%. The full font set is 735 KiB and buys back
  nothing by shrinking. Do not re-open this without a new measurement.

- **`lv_keyboard` positions itself, and `lv_obj_set_pos()` then means something else.**
  Its constructor calls `lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)` on itself
  (`lv_keyboard.c`), and in LVGL 9 an object's x/y become an OFFSET FROM ITS ALIGNMENT once
  one is set. So `lv_obj_set_pos(kb, 0, 240)` — which is how every other widget in this
  codebase is placed, and which reads as obviously correct — asks for a keyboard 240 px
  **below the bottom edge of the panel**. The screen renders perfectly, with no keyboard on
  it, and nothing is logged. **The WLAN password step shipped like this from M6 until
  2026-09-20** and nobody saw it, because that step needs a finger on an unknown network
  and so was never once reached from the build host. Use `lv_obj_align(kb,
  LV_ALIGN_TOP_LEFT, 0, y)`. Both keyboards are styled by `main/ui/widget_input.c`, which
  says the same thing where someone writing the next one will read it.
- **`lv_keyboard_set_map()` is PROCESS-WIDE, not per keyboard.** It writes into a
  file-scope table inside `lv_keyboard.c` (`kb_map[mode] = map`) that every keyboard reads
  at redraw, so two keyboards cannot have two layouts and installing one from either screen
  changes both. Here that is what we want — the WLAN keyboard and the Ortssuche keyboard
  must be the same keyboard — so `widget_input.c` leans on it and installs the German
  QWERTZ layout from the styling function. If you ever DO need two, the only per-instance
  hook is `LV_KEYBOARD_MODE_USER_1..4`.
- **The three layer keys are a contract with LVGL, spelled by hand.**
  `lv_keyboard_def_event_cb()` decides whether a key switches layer by comparing its CAP
  TEXT against `LV_KEYBOARD_CTRL_BUTTON_MODE_TEXT_LOWER` / `_UPPER` / `_SPECIAL` — macros
  `lv_keyboard.c` keeps to itself and does not export, so a custom map has to carry the same
  three strings (`"abc"`, `"ABC"`, `"1#"`). Get one wrong and nothing warns: the key stops
  switching layers and starts typing its own cap into the field. `widget_keyboard_debug_layer()`
  and the `a` console key exist to press all three and photograph the result, because this
  is the kind of thing that breaks silently on an LVGL bump.
- **The built-in Montserrat faces have no umlauts.** LVGL generates them with
  `-r 0x20-0x7F,0xB0,0x2022` plus FontAwesome — read it off the top of
  `lv_font_montserrat_24.c`, it is in the file. So an `ü` key drawn in Montserrat is a key
  with nothing on it; and the Plex subset has none of the `LV_SYMBOL_*` private-use
  codepoints, so a backspace drawn in Plex is a key with nothing on it either. Neither face
  can draw a German keyboard alone. The way out is a per-state font: `lv_buttonmatrix`
  re-reads `LV_PART_ITEMS`'s label style per button with that button's own state
  (`lv_buttonmatrix.c`, `draw_main`), and every control key carries
  `LV_BUTTONMATRIX_CTRL_CHECKED` — so `LV_PART_ITEMS` gets Plex and
  `LV_PART_ITEMS | LV_STATE_CHECKED` gets Montserrat.
- **`LV_LABEL_LONG_MODE_DOTS` needs a fixed HEIGHT, not just a fixed width.** Its
  implementation only ellipsises when the rendered text is taller than the object
  (`lv_label.c`: `size.y > lv_area_get_height(&txt_coords)`), so a label given a width but
  left at `LV_SIZE_CONTENT` height does not truncate — it grows another line, and draws it
  over whatever was laid out underneath. That is how the WLAN screen ended up with
  "Verbindung fehlgeschlagen:" on one line, the SSID on a second, and the network list
  painted on top of the second. **The code reads as though the problem had been fixed**,
  which is the whole trap: setting the width and asking for dots looks like the complete
  gesture. Pin both dimensions, and lay the band below out from the font's line height
  rather than from the label's measured one. **It was in the file twice**: every SSID in
  the WLAN list had the same missing height, and the network this device is actually on —
  `Apartamentos_Jose_Cruz` — had been breaking across two lines inside a 64 px row since
  M6. Both were found by looking at the glass, neither by reading the code.
- **`lv_button` arrives padded, and both `lv_obj_set_pos()` and `lv_obj_align()` measure
  from the CONTENT area.** LVGL's default theme gives a button `pad_hor = PAD_DEF` and
  `pad_ver = PAD_SMALL` — about 13 px and 8 px at this panel's 130 DPI — so a label placed
  at `(ROW_INSET, ROW_PAD_V)` inside one actually lands 13 px right and 8 px down of that,
  while any width computed from the row's own `CONTENT_W` overflows the content box by
  26 px. The visible result is subtle and looks like a different bug: an ellipsis drawn
  through the badge beside it, or a two-line row sitting too low. `widget_kill_button_chrome()`
  does NOT cover this — it clears the shadow and the outline, not the padding. Any
  `lv_button` used as a layout container wants an explicit `lv_obj_set_style_pad_all(b, 0, 0)`
  so that the insets in the code are the insets on the panel.
- **An LVGL timer outlives the object it reads, and every debug view frees that object.**
  `lv_timer_create()` runs until something deletes it; `lv_obj_clean(lv_screen_active())`,
  which `dbg_fontcard.c` and `dbg_bench.c` both call, deletes widgets and tells nobody. A
  poller reading a screen's own pointer is then a LoadProhibited a quarter of a second
  later, from a stack with no application frame in it. Keep the handle, delete it from an
  `LV_EVENT_DELETE` handler on the object it reads, and guard the callback — the same
  arrangement `widget_busy.c` uses to tie an animation's life to its object (D58). Note
  also that a `*_create()` called again on `ui_resume()` creates a SECOND timer: the old
  one has to go first.
- **LVGL's default theme draws a shadow under every `lv_button`**, which on this ground is
  a 2 px band of `#525152` all round — a grey line under every list divider and a grey
  column down both edges of a list. Nothing in the source asks for it, so nothing in the
  source looks wrong; it was found by reading the panel's framebuffer back and probing
  pixels. `widget_kill_button_chrome()`.
- **In LVGL 9 every `lv_obj_create()` is a touch target, and no event ever bubbles.**
  The `lv_obj` constructor sets `obj->clickable = 1` (labels are the exception — theirs
  sets it false), and LVGL passes an event to a parent only if the child carries
  `LV_OBJ_FLAG_EVENT_BUBBLE`. So a full-screen container a screen creates for layout
  silently eats every press aimed at anything underneath it, and a decorative
  `lv_obj_create()` circle eats every press inside its bounding box — which for the radar's
  range rings is most of the panel. Scrolling is not affected, because scrolling searches
  UP the parent chain for a scrollable ancestor; clicking does not. This cost the long press
  into Einstellungen its entire life (D62). `nav.c`'s `bubble_decorative()` is the rule that
  came out of it: an object with no event callback of its own is scenery and passes touches
  on.

**Power and the battery**
- **The BSP does not touch the AXP2101 at all** — `grep -i axp` over the Waveshare component
  returns nothing. Everything about charging is `main/power/axp2101.c`, and before it existed
  the charger ran on whatever the chip's eFuse said.
- **REG50[4] must be set or the cell may never charge.** The TS pin is wired to a plain
  resistor to ground, not a thermistor, and that bit's reset value comes from the eFuse.
  Waveshare's own example disables it with the comment "otherwise it will cause abnormal
  charging". The symptom is a device that looks fine and quietly stays flat — the Akku line
  in Einstellungen says "wird nicht geladen" for exactly this case.
- **Never write a rail.** REG80/REG90 and friends turn the panel or the ESP32 off, and the
  only way back is the PWRKEY on the side edge. The driver touches the charger, the ADC and
  the gauge, nothing else.
- **A zeroed `battery_status_t` means a black screen.** Its `brightness_cap_pct` is 0 and the
  dimmer takes the minimum of the schedule and the cap, so the one in main.c is initialised
  explicitly. Copy that if you ever add another.

**Fonts and text**
- **`lv_font_conv` does not apply OpenType features.** A font whose tabular figures exist
  only behind the `tnum` feature will render digits that visibly jitter on every refresh.
  Use a font that is tabular *by default* — IBM Plex Mono is; Barlow Condensed, Saira
  Condensed and Oswald are not.
- **`lv_font_conv` compresses glyph bitmaps by default, and LVGL 9 will not decode
  them.** `.bitmap_format = 1` needs `LV_USE_FONT_COMPRESSED`, which is off in this
  build — so every glyph renders as *nothing at all*, with no error logged anywhere.
  Generate with `--no-compress`. Uncompressed is the better trade regardless: it costs
  flash but no per-frame CPU, and this product is render-bound.
- **`lv_font_conv` predates LVGL 9.3's `.static_bitmap` flag** and cannot emit it, so
  `tools/build_fonts.sh` patches it in after generating. Without it LVGL copies every
  glyph instead of using the const data in place.
- **Umlauts are not in the default ASCII range.** Subset Latin-1 supplement explicitly or
  ä/ö/ü/ß render as blanks. Exact ranges in docs/DESIGN.md §3.
- **Minimum readable cap height on this panel is ~30 px** (ISO 9241-303 at 70 cm). Chrome
  may be smaller; anything he needs at a glance may not.

**Data parsing**
- **`flight` is space-padded to 8 characters** (`"AUA453  "`). Trim before sending to
  `routeset` or lookups will silently miss.
- **`alt_baro` is the string `"ground"`**, not a number, when the aircraft is on the
  ground. Parsing it as an int will break.
- **`r` (registration) and `t` (type) can be absent** — military, blocked, and TIS-B
  targets. Always null-check.
- **Parse with cJSON, not ArduinoJson.** This is ESP-IDF; cJSON already ships with it, and
  that is what lets the *same* parser link into the host tests (D6, D7). You need ~6 of 50+
  fields per aircraft: pull those into the fixed-size struct and free the document, rather
  than keeping it alive.
- Send a **descriptive User-Agent** on every request — planespotters.net rejects generic
  ones outright, and it is the courteous thing to do with free services. It identifies the
  project by **repo URL, not by email**: `HTTP_USER_AGENT` in `main/net/http_get.h`. The
  address in the git history is for authorship; it does not get handed to a third party.
  This was deliberate — do not "improve" the UA by putting contact details back in.

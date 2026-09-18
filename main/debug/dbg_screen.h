/* On-device visual verification.
 *
 * We cannot see the panel from the build host, so "does it look right" is
 * otherwise unanswerable without a human in the room. This reads the RGB
 * framebuffer the panel is actually scanning out and ships it over USB as
 * base64, CRC-checked. tools/grab_screen.py turns it back into a PNG.
 *
 * This is debug scaffolding, not product code: it costs nothing at runtime
 * until a command byte arrives.
 */
#pragma once
void dbg_screen_start(void);

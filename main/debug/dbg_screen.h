/* On-device visual verification and debug console.
 *
 * We cannot see the panel from the build host, so "does it look right" is
 * otherwise unanswerable without a human in the room. 's' reads the RGB
 * framebuffer the panel is actually scanning out and ships it over USB as
 * CRC-checked base64; tools/grab_screen.py turns it back into a PNG.
 *
 * Debug scaffolding, not product code: costs nothing until a byte arrives.
 */
#pragma once

/* Called for any command byte dbg_screen does not handle itself. */
typedef void (*dbg_cmd_fn)(char c);

void dbg_screen_start(dbg_cmd_fn on_cmd);

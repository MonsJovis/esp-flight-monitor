/* The M1 font gate, drawn so a screenshot can answer it. Debug console key 'f'.
 *
 * Takes over the screen: call ui_suspend() first and ui_resume() ('0') after,
 * like every other debug view.
 */
#pragma once

void dbg_font_card(void);

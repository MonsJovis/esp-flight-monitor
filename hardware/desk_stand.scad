/* Desk stand for the Waveshare ESP32-S3-Touch-LCD-4B ("Smart 86 Box").
 *
 * The board ships as a flush 86-type wall plate — 86.5 x 86.5 x 14 mm — which
 * is exactly the wrong shape to stand on a table. This is the missing piece:
 * a single-part wedge that holds it at a readable angle with the side-edge
 * USB-C cable running out of the way.
 *
 * ---------------------------------------------------------------------------
 * NOT PRINTED. NOT MEASURED ON THE REAL UNIT.
 *
 * Every dimension below comes from the datasheet figures recorded in
 * AGENTS.md §2, not from calipers on the device in hand, and no test print has
 * been made. Print the FIT TEST first (see `part` below): it is the slot and
 * the lip only, about four minutes of filament, and it will tell you whether
 * DEVICE_T and SLOT_CLEARANCE are right before you commit to an hour.
 * ---------------------------------------------------------------------------
 *
 * Why 20 degrees. The device is read at roughly 70 cm by someone seated, which
 * is the same viewing geometry DESIGN.md uses for its ISO 9241-303 character
 * height floor. Eye height above a table is around 35-40 cm, so the sight line
 * arrives about 25-30 degrees above horizontal; tipping the panel back 20 from
 * vertical puts the glass close to square to it without making the stand so
 * deep that it dominates the table. An LCD viewed off-axis loses contrast, and
 * contrast is the whole argument of the colour system.
 *
 * Units are millimetres. OpenSCAD: https://openscad.org (GPL-2.0).
 */

/* ---- what to render ---------------------------------------------------- */
// "stand"    the whole thing
// "fittest"  just the slot and lip, for checking the fit before a long print
part = "stand";

/* ---- the device -------------------------------------------------------- */
DEVICE_W = 86.5;   // AGENTS.md §2, faceplate width
DEVICE_H = 86.5;   // faceplate height
DEVICE_T = 14.0;   // faceplate depth. THE ONE TO VERIFY: the case may sit
                   // proud of this, and the slot is unforgiving if it does.

/* ---- the stand --------------------------------------------------------- */
TILT            = 20;   // degrees back from vertical
WALL            = 3.0;  // everything structural
SLOT_CLEARANCE  = 0.6;  // per side. 0.6 suits a 0.4 mm nozzle at 0.2 mm layers;
                        // raise it before you file anything down.
LIP_H           = 11;   // how far the front lip rises up the glass. Tall enough
                        // that a knock does not tip it out, short enough to
                        // clear the 480 px active area, which starts a few mm
                        // in from the faceplate edge.
BASE_MARGIN     = 6;    // base overhang in front of and behind the device, for
                        // the tipping moment
SIDE_EXTRA      = 4;    // base wider than the device, each side

CABLE_NOTCH_W   = 14;   // USB-C plug body plus a cheap boot
CABLE_NOTCH_H   = 9;
CABLE_SIDE      = 1;    // 1 = right as you look at the screen, -1 = left

$fn = 48;

/* ---- derived ----------------------------------------------------------- */
SLOT_T   = DEVICE_T + 2 * SLOT_CLEARANCE;
// Footprint the leaning device actually occupies, front face to back face.
LEAN_D   = DEVICE_T * cos(TILT) + DEVICE_H * sin(TILT);
BASE_D   = LEAN_D + 2 * BASE_MARGIN;
BASE_W   = DEVICE_W + 2 * SIDE_EXTRA;
// Back rest reaches ~55% up the device: enough to carry the lean, low enough
// that the body of the stand never enters the sight line.
BACK_H   = DEVICE_H * 0.55;

module side_profile() {
    // Drawn in X (depth) / Y (height), extruded across the width afterwards.
    difference() {
        union() {
            // Base slab.
            square([BASE_D, WALL]);
            // Back rest: a wedge leaning at TILT, thick at the bottom.
            translate([BASE_MARGIN + DEVICE_T * cos(TILT), 0])
                polygon([[0, 0],
                         [WALL * 2 + BACK_H * sin(TILT), 0],
                         [WALL * 2 + BACK_H * sin(TILT), WALL],
                         [BACK_H * sin(TILT) + WALL, BACK_H],
                         [BACK_H * sin(TILT), BACK_H]]);
            // Front lip.
            translate([BASE_MARGIN - WALL, 0])
                polygon([[0, 0],
                         [WALL, 0],
                         [WALL + LIP_H * sin(TILT), LIP_H],
                         [LIP_H * sin(TILT), LIP_H]]);
        }
        // The slot the device drops into — cut right through the base so a
        // grain of grit cannot hold the panel proud of its seat.
        translate([BASE_MARGIN, -1])
            rotate([0, 0, -TILT])
                translate([0, 1])
                    square([SLOT_T, DEVICE_H + 2]);
    }
}

module stand() {
    difference() {
        linear_extrude(height = BASE_W) side_profile();

        // Cable relief: a notch through the base and the front lip, on one
        // side, so a right-angle USB-C plug can leave sideways under the
        // glass instead of pushing the stand off the table.
        translate([BASE_MARGIN - WALL - 1,
                   -1,
                   CABLE_SIDE > 0 ? BASE_W - SIDE_EXTRA - CABLE_NOTCH_W
                                  : SIDE_EXTRA])
            cube([WALL + SLOT_T + 4, CABLE_NOTCH_H + 1, CABLE_NOTCH_W]);
    }
}

module fittest() {
    // 20 mm of the real thing, across the slot and the lip only.
    intersection() {
        stand();
        translate([-1, -1, (BASE_W - 20) / 2]) cube([BASE_D + 2, 60, 20]);
    }
}

if (part == "fittest") fittest(); else stand();

/* Printing, for whoever runs it:
 *
 *   As modelled, the part lies on its side — the flat face against the bed,
 *   the layers running across the width. That is the orientation you want:
 *   the load on the back rest is then a shear across layer lines rather than
 *   a peel along them, and nothing needs support.
 *
 *   PETG or PLA, 3 perimeters, 20% infill. PLA is fine indoors; it is not
 *   fine in a car in Pattaya in April.
 *
 *   Print `part = "fittest"` first.
 */

// rack-fancontrol enclosure — base plate with module snap-cradles.
// Parametric. All dimensions in mm.
// Print FDM, 0.2 mm layers, 3 perimeters, 20 % infill.
//
// After first test print, fine-tune per-module dimensions with calipers
// against the real modules and re-render.

$fn = 48;

// ========== tolerances ==========
slop       = 0.3;   // extra clearance around PCB footprint
clip_over  = 0.8;   // how far the clip lip overhangs the PCB top

// ========== plate ==========
plate_thick = 3;
plate_l     = 160;  // X  -- short sides host the cable entries
plate_w     = 90;   // Y
corner_r    = 3;

// ========== typical module dimensions ==========
// [length, width, height, pcb_thick]  (height = tallest component on PCB)
esp_dim    = [70, 26, 20, 1.6];   // ESP32-S3 DevKitC-1 (USB-C + header)
ina_dim    = [22, 17,  8, 1.6];   // GY-219 breakout
buck_dim   = [30, 20, 12, 1.6];   // XW026FR4
mosfet_dim = [35, 22, 12, 1.6];   // 60N03 module
shift_dim  = [25, 20,  6, 1.6];   // BSS138 4-ch level shifter

// The ESP DevKit is mounted FLIPPED (pin headers facing up so you can
// plug Dupont wires in directly from above). That requires extra
// clearance under the PCB for the chip + WiFi-antenna + bottom-side
// components -- typical DevKitC-1 is ~4 mm tall on the chip side, so
// 8 mm standoff gives margin. USB-C end points towards the enclosure's
// left short side (x=0) so the programming cable exits cleanly.
esp_standoff = 8;
default_standoff = 1.5;

// WAGO 221-415 holder -- using community STL (Printables "Wago 221-415
// Connector x 5 Mount" by Wiseone, 1x variant downloaded as wago221_x1.stl).
// Bounding box of the STL: X 23.2, Y 30.0, Z 12.8.  STL origin has the
// long axis along Y (negative) and a small tab hanging at negative X.
wago_stl_size   = [23.2, 30.0, 12.8];
wago_stl_offset = [-2.75, -30.0, -2.75];  // min-corner offset in STL frame
wago_count      = 3;
wago_gap        = 3;

// ========== module positions ==========
// Layout (X = 0 at left, Y = 0 at bottom, both in "plate" frame):
//   Bottom row  — WAGOs (cable clamps / terminal strip)
//   Middle row  — Buck, Level-Shifter, INA
//   Top row     — ESP32-S3, MOSFET module
//
// Change these to move things around; snap_cradle() consumes them.

wago_y = 6;
esp_pos   = [  8, 55];
mosfet_pos= [ 90, 58];
buck_pos  = [  6, 32];
shift_pos = [ 45, 33];
ina_pos   = [ 80, 34];

// ========== primitives ==========

// rounded rectangular base
module plate() {
    linear_extrude(plate_thick)
    offset(r = corner_r) offset(delta = -corner_r)
        square([plate_l, plate_w], center = false);
}

// A single snap clip standing on the plate top at (x,y) with the
// retaining lip facing in direction `dir` (unit vector, inward towards
// the PCB center).  Clips are printed vertically, no supports.
module snap_clip(pcb_thick) {
    pillar_w = 4;          // along PCB edge
    pillar_t = 2;          // perpendicular to PCB edge
    lip_h    = 1.2;
    pillar_h = pcb_thick + lip_h + 0.4;

    // body pillar
    cube([pillar_w, pillar_t, pillar_h]);
    // inward-facing lip: sloped triangular prism so PCB slides past it
    translate([0, pillar_t, pillar_h - lip_h])
        rotate([90, 0, 0])
        linear_extrude(pillar_t + clip_over)
        polygon([[0, 0], [pillar_w, 0], [pillar_w, lip_h], [0, lip_h]]);
}

// Four snap clips at corners of a module footprint, plus standoff pads
// in the corners so the PCB doesn't scrape the plate.  `standoff_h`
// sets the PCB's top surface above the plate; the snap clips grow to
// match so the lip still catches the PCB edge.
module snap_cradle(pos, dim, standoff_h = default_standoff) {
    l = dim[0]; w = dim[1]; pcb_t = dim[3];
    translate([pos[0], pos[1], plate_thick]) {
        // 4 standoff columns at the PCB corners (form the cradle floor)
        for (sx = [3, l - 3], sy = [3, w - 3])
            translate([sx, sy, 0])
                cylinder(h = standoff_h, d = 4);

        // 4 snap clips at the PCB edges
        translate([0, 0, standoff_h]) {
            translate([-2, -2, 0]) snap_clip(pcb_t);
            translate([l + 2, -2, 0])
                mirror([1, 0, 0]) snap_clip(pcb_t);
            translate([-2, w + 2, 0])
                mirror([0, 1, 0]) snap_clip(pcb_t);
            translate([l + 2, w + 2, 0])
                mirror([0, 1, 0]) mirror([1, 0, 0]) snap_clip(pcb_t);
        }
    }
}

// WAGO 221 holder: imports community STL, re-anchors so the STL's
// min-corner sits at (pos, plate_thick) on our plate.
module wago_holder(pos) {
    translate([pos[0] - wago_stl_offset[0],
               pos[1] - wago_stl_offset[1],
               plate_thick - wago_stl_offset[2]])
        import("wago221_x1.stl", convexity = 5);
}

// ========== assembly ==========

module base_plate() {
    difference() {
        union() {
            plate();

            // module snap-cradles
            snap_cradle(esp_pos,    esp_dim,    esp_standoff);
            snap_cradle(mosfet_pos, mosfet_dim);
            snap_cradle(buck_pos,   buck_dim);
            snap_cradle(shift_pos,  shift_dim);
            snap_cradle(ina_pos,    ina_dim);

            // WAGO row -- 3x imported STL holders side by side
            for (i = [0 : wago_count - 1])
                wago_holder([ 10 + i * (wago_stl_size[0] + wago_gap), wago_y ]);
        }

        // ventilation slots under ESP + MOSFET for airflow inside the
        // future enclosure (cheap convection)
        for (x = [15, 30, 45, 100, 115, 130])
            translate([x, 40, -0.1])
                cube([3, 10, plate_thick + 0.2]);
    }
}

base_plate();

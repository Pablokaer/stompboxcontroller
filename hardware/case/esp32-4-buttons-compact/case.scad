// STOMPBOX Controller – compact ESP32-C3 SuperMini case, 4 conventional footswitches.
// Sized only for: the ESP32-C3 SuperMini with its pin headers pointing UP and
// female Dupont jumpers on them, plus four 12 mm soft-touch footswitches 35 mm apart.
// 2 mm walls and floor, 3.5 mm lid. Units: mm.
//
//   openscad -D 'part="base"' -o base.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=true'  -o lid.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=false' -o lid_no_labels.stl case.scad
part = "assembly";            // "base" | "lid" | "assembly"
show_labels = true;

// ---------- footswitches (12 mm soft-touch SPST) – check against yours ----------
sw_hole = 12.4;               // M12 thread + clearance
sw_nut = 14;                  // nut across flats (16 mm across corners)
sw_depth = 27;                // depth below the lid incl. solder lugs
nut_r = sw_nut / cos(30) / 2 + 0.4;
body_d = 12;
pitch = 35;
xs = [-1.5, -0.5, 0.5, 1.5] * pitch;
labels = ["MOD", "DELAY", "REVERB", "EQ"];

// ---------- walls ----------
wall = 2; floor_t = 2; lid_t = 3.5;
R = 1.5;

// ---------- ESP32-C3 SuperMini, headers up, Dupont jumpers on top ----------
pcb_l = 22.6; pcb_w = 18.2; pcb_t = 1.0;
header_h = 2.5;               // plastic spacer of the pin header
dupont_h = 14;                // female Dupont housing
wire_bend = 5;                // room for the wires to bend over
cradle_h = 1.5; rail = 0.8;
stack_h = cradle_h + pcb_t + header_h + dupont_h + wire_bend;   // board tower above the floor
usb_w = 13; usb_h = 8;
usb_z = floor_t + cradle_h + pcb_t + 1.6;

// ---------- size: just enough ----------
inner_h = max(sw_depth + 3, stack_h + 1);
base_h = floor_t + inner_h;
H = base_h + lid_t;
inner_hl = max(xs) + nut_r + 0.5;
inner_hw = max(pcb_l + rail, 2 * nut_r + 1) / 2;
L = 2 * (inner_hl + wall);
W = 2 * (inner_hw + wall);

// ---------- lid screws: 2 x M3 between the outer and inner switches ----------
screw_x = [-pitch, pitch];
boss_d = 7.5; insert_d = 4.0;          // M3 heat-set insert (2.5 for self-tapping)
screw_d = 3.4; csk_d = 6.4;
brace_h = 6;

lip_t = 1.2; lip_h = 2.5; lip_gap = 0.3;
label_size = 3.4; label_depth = 0.6; label_off = 10.2;

$fn = 64;
echo(str("Compact ESP32 case outer size: ", L, " x ", W, " x ", H, " mm"));
assert(pitch - body_d >= pcb_w + 2 * rail + 1, "switches too close: the board no longer fits between the middle ones");
assert(nut_r < inner_hw - lip_gap - lip_t, "switch nut would hit the lid lip");

module rbox(l, w, h, r) { linear_extrude(h) offset(r) square([l - 2*r, w - 2*r], center=true); }

module base() {
  difference() {
    union() {
      difference() {
        rbox(L, W, base_h, R);
        translate([0, 0, floor_t]) rbox(L - 2*wall, W - 2*wall, base_h, 0.5);
      }
      for (x = screw_x) {
        translate([x, 0, 0]) cylinder(d=boss_d, h=base_h);
        translate([x - 1, -W/2 + 1, 0]) cube([2, W - 2, floor_t + brace_h]);
      }
      // board cradle: platform + thin side rails + front stop; USB-C at the back wall
      translate([-pcb_w/2 - rail, W/2 - wall - pcb_l - rail, 0]) difference() {
        cube([pcb_w + 2*rail, pcb_l + rail, floor_t + cradle_h + 1.5]);
        translate([rail, rail, floor_t + cradle_h]) cube([pcb_w, pcb_l + 1, 5]);
      }
    }
    for (x = screw_x) translate([x, 0, base_h - 8]) cylinder(d=insert_d, h=9);
    translate([0, W/2 + 1, usb_z]) rotate([90, 0, 0])
      linear_extrude(wall + 2) offset(2) square([usb_w - 4, usb_h - 4], center=true);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * (L/2 - 9), sy * (W/2 - 7), -0.01]) cylinder(d=9, h=0.8);
  }
}

module lid() {
  difference() {
    union() {
      rbox(L, W, lid_t, R);
      // alignment lip along both long walls (switch nuts stay clear of it)
      for (s = [-1, 1])
        translate([-(inner_hl - 4), s > 0 ? inner_hw - lip_gap - lip_t : -inner_hw + lip_gap, -lip_h])
          cube([2 * (inner_hl - 4), lip_t, lip_h]);
    }
    for (x = xs) translate([x, 0, -10]) cylinder(d=sw_hole, h=30);
    for (x = screw_x) translate([x, 0, 0]) {
      translate([0, 0, -10]) cylinder(d=screw_d, h=30);
      translate([0, 0, lid_t - 1.8]) cylinder(d1=screw_d, d2=csk_d, h=1.81);
    }
    if (show_labels) for (i = [0:3])
      translate([xs[i], -label_off, lid_t - label_depth]) linear_extrude(label_depth + 1)
        text(labels[i], size=label_size, font="DejaVu Sans:style=Bold", halign="center", valign="center");
  }
}

// ---------- preview helpers (not printed) ----------
module board() {
  translate([-pcb_w/2, W/2 - wall - pcb_l, floor_t + cradle_h]) {
    color("#1f2a44") cube([pcb_w, pcb_l, pcb_t]);
    color("silver") translate([pcb_w/2 - 4.5, pcb_l - 7.3, pcb_t]) cube([9, 7.5, 3.2]);
    // two 8-pin headers along the long edges + Dupont housings on top
    for (sx = [1.3, pcb_w - 1.3]) for (i = [0:7]) translate([sx, 1.2 + i * 2.54, pcb_t]) {
      color("#222") translate([-1.27, -1.27, 0]) cube([2.54, 2.54, header_h]);
      color("#d4a017") translate([-0.3, -0.3, header_h]) cube([0.6, 0.6, 6]);
      if (i < 5) color("#333") translate([-1.27, -1.27, header_h]) cube([2.54, 2.54, dupont_h]);
    }
  }
}
module footswitch() {
  color("silver") translate([0, 0, -sw_depth]) cylinder(d=body_d, h=sw_depth + lid_t + 8);
  color("#333") translate([0, 0, lid_t + 3]) cylinder(d=15, h=8);
  color("silver") translate([0, 0, lid_t]) cylinder(d=sw_nut / cos(30), h=3, $fn=6);
  color("silver") translate([0, 0, -3]) cylinder(d=sw_nut / cos(30), h=3, $fn=6);
}

if (part == "base") base();
else if (part == "lid") lid();
else {
  color("#3a3f45") base();
  board();
  translate([0, 0, base_h]) {
    color("#d9dde0", 0.9) lid();
    for (x = xs) translate([x, 0, 0]) footswitch();
  }
}

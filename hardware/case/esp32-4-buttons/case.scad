// STOMPBOX Controller – ESP32-C3 SuperMini case, 4 footswitches in one row.
// Same 40 mm button spacing as the 4-buttons-slim Raspberry Pi case, 5 mm lower,
// and only as long / wide as the switches, screws and board need. Units: mm.
//
//   openscad -D 'part="base"' -o base.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=true'  -o lid.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=false' -o lid_no_labels.stl case.scad
part = "assembly";            // "base" | "lid" | "assembly" (preview with board + switches)
show_labels = true;

// ---------- footswitches (12 mm soft-touch SPST) ----------
pitch = 40;                   // same as the Pi slim case
xs = [-1.5, -0.5, 0.5, 1.5] * pitch;
labels = ["MOD", "DELAY", "REVERB", "EQ"];
sw_hole = 12.4;               // M12 thread + clearance
nut_r = 8.5;                  // hex nut / washer under the lid (16 mm across corners) + margin

// ---------- walls and height ----------
wall = 3; floor_t = 3; lid_t = 4;
H = 43;                       // total height (Pi slim case: 48)
base_h = H - lid_t;           // 39
inner_h = base_h - floor_t;   // 36: room for the switch bodies (~27 mm) above the floor
R = 1.5;

// ---------- ESP32-C3 SuperMini ----------
pcb_l = 22.6; pcb_w = 18.2; pcb_t = 1.0;   // board, long side across the case
cradle_h = 1.5;               // raises the board so a USB-C plug fits the wall hole
usb_w = 13; usb_h = 8;        // opening for the USB-C plug's moulding
usb_z = floor_t + cradle_h + pcb_t + 1.6;  // centre of the USB-C socket

// ---------- size: just enough ----------
inner_hl = max(xs) + nut_r + 0.5;          // half length inside: last switch + nut
inner_hw = max(pcb_l + 1.5, 2 * nut_r + 6) / 2 + 1;   // board lies across; room for wires
L = 2 * (inner_hl + wall);
W = 2 * (inner_hw + wall);

// ---------- lid screws: 2 x M3 between the switches (the board sits in the middle gap) ----------
screw_x = [-pitch, pitch];
boss_d = 8; insert_d = 4.0;   // M3 heat-set insert (2.5 for self-tapping screws)
screw_d = 3.4; csk_d = 6.6;

lip_t = 1.6; lip_h = 3; lip_gap = 0.4;
brace_h = 8;                  // height of the webs bracing the screw bosses (keep wires above)
label_size = 4.2; label_depth = 0.6; label_off = 10.5;

$fn = 64;
echo(str("ESP32 case outer size: ", L, " x ", W, " x ", H, " mm"));

module rbox(l, w, h, r) { linear_extrude(h) offset(r) square([l - 2*r, w - 2*r], center=true); }

module base() {
  difference() {
    union() {
      difference() {
        rbox(L, W, base_h, R);
        translate([0, 0, floor_t]) rbox(L - 2*wall, W - 2*wall, base_h, 0.5);
      }
      // screw bosses, braced to both long walls by low webs (wires pass over them)
      for (x = screw_x) {
        translate([x, 0, 0]) cylinder(d=boss_d, h=base_h);
        translate([x - 1, -W/2 + 1, 0]) cube([2, W - 2, floor_t + brace_h]);
      }
      // board cradle: platform + low side rails + stop, USB-C at the back wall
      translate([-pcb_w/2 - 1.2, W/2 - wall - pcb_l - 1.2, 0]) difference() {
        cube([pcb_w + 2.4, pcb_l + 1.2, floor_t + cradle_h + 2]);
        translate([1.2, 1.2, floor_t + cradle_h]) cube([pcb_w, pcb_l + 1, 5]);
      }
    }
    for (x = screw_x) translate([x, 0, base_h - 10]) cylinder(d=insert_d, h=11);
    // USB-C opening in the back wall
    translate([0, W/2 + 1, usb_z]) rotate([90, 0, 0])
      linear_extrude(wall + 2) offset(2) square([usb_w - 4, usb_h - 4], center=true);
    // rubber-foot recesses
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * (L/2 - 10), sy * (W/2 - 8), -0.01]) cylinder(d=10.5, h=1);
  }
}

module lid() {
  difference() {
    union() {
      rbox(L, W, lid_t, R);
      // alignment lip along the long walls (the ends stay free for the outer switch nuts)
      for (s = [-1, 1])
        translate([-(inner_hl - 12), s > 0 ? inner_hw - lip_gap - lip_t : -inner_hw + lip_gap, -lip_h])
          cube([2 * (inner_hl - 12), lip_t, lip_h]);
    }
    // switch holes
    for (x = xs) translate([x, 0, -10]) cylinder(d=sw_hole, h=30);
    // countersunk M3 screw holes
    for (x = screw_x) translate([x, 0, 0]) {
      translate([0, 0, -10]) cylinder(d=screw_d, h=30);
      translate([0, 0, lid_t - 1.8]) cylinder(d1=screw_d, d2=csk_d, h=1.81);
    }
    // engraved labels in front of each switch (player side = -Y)
    if (show_labels) for (i = [0:3])
      translate([xs[i], -label_off, lid_t - label_depth]) linear_extrude(label_depth + 1)
        text(labels[i], size=label_size, font="DejaVu Sans:style=Bold", halign="center", valign="center");
  }
}

// ---------- preview helpers (not printed) ----------
module board() {
  translate([-pcb_w/2, W/2 - wall - pcb_l, floor_t + cradle_h]) {
    color("#1f2a44") cube([pcb_w, pcb_l, pcb_t]);
    color("silver") translate([pcb_w/2 - 4.5, pcb_l - 7.3, pcb_t]) cube([9, 7.5, 3.2]);   // USB-C
  }
}
module footswitch() {
  color("silver") translate([0, 0, -24]) cylinder(d=12, h=24 + lid_t + 8);
  color("#333") translate([0, 0, lid_t + 3]) cylinder(d=15, h=8);
  color("silver") translate([0, 0, lid_t]) cylinder(d=16, h=3, $fn=6);
  color("silver") translate([0, 0, -3]) cylinder(d=16, h=3, $fn=6);      // nut under the lid
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

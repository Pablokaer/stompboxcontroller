// STOMPBOX Controller – compact ESP32-C3 SuperMini case, 4 buttons in one row.
// Uses 16 mm anti-vandal (stainless, flat head) momentary push buttons instead of
// guitar footswitches: much shallower, so the case is ~12 mm lower and shorter.
// Thin walls (2 mm), 35 mm button spacing. Units: mm.
//
//   openscad -D 'part="base"' -o base.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=true'  -o lid.stl case.scad
//   openscad -D 'part="lid"' -D 'show_labels=false' -o lid_no_labels.stl case.scad
//
// CHECK YOUR BUTTONS: set the four values below from their datasheet before printing.
part = "assembly";            // "base" | "lid" | "assembly"
show_labels = true;

// ---------- 16 mm anti-vandal push button (typical values, e.g. C&K ATP16 flat) ----------
btn_hole = 16.3;              // M16x1 thread + clearance (datasheets: 16 / 16.2 mm cutout)
btn_nut = 19;                 // nut across flats (ATP16: 19 mm)
btn_depth = 20;               // depth behind the panel incl. solder lugs (flat type ~14-20 mm)
btn_head = 19;                // head diameter above the panel (for the preview only)
nut_r = btn_nut / cos(30) / 2 + 0.5;   // nut corner radius + margin

pitch = 35;
xs = [-1.5, -0.5, 0.5, 1.5] * pitch;
labels = ["MOD", "DELAY", "REVERB", "EQ"];

// ---------- walls ----------
wall = 2; floor_t = 2; lid_t = 3;      // lid 3 mm: inside the 1-6 mm panel range of the buttons
R = 1.5;

// ---------- ESP32-C3 SuperMini (between the two middle buttons) ----------
pcb_l = 22.6; pcb_w = 18.2; pcb_t = 1.0;
cradle_h = 1.5; rail = 0.8;
usb_w = 13; usb_h = 8;
usb_z = floor_t + cradle_h + pcb_t + 1.6;

// ---------- size: just enough ----------
inner_h = btn_depth + 6;               // button body + room for the soldered wires
base_h = floor_t + inner_h;
H = base_h + lid_t;
inner_hl = max(xs) + nut_r + 0.5;
inner_hw = max(pcb_l + 1.2, 2 * nut_r + 1) / 2;
L = 2 * (inner_hl + wall);
W = 2 * (inner_hw + wall);

// ---------- lid screws: 2 x M3 between the outer and inner buttons ----------
screw_x = [-pitch, pitch];
boss_d = 7.5; insert_d = 4.0;          // M3 heat-set insert (2.5 for self-tapping)
screw_d = 3.4; csk_d = 6.4;
brace_h = 6;

lip_t = 1.2; lip_h = 2.5; lip_gap = 0.3;
lip_segments = [0, -pitch, pitch];     // short lip pieces where no button nut is
lip_len = 16;
label_size = 3.2; label_depth = 0.5; label_off = 11.6;

$fn = 64;
echo(str("Compact ESP32 case outer size: ", L, " x ", W, " x ", H, " mm"));
assert(pitch - 16 >= pcb_w + 0.4, "buttons too close: the board no longer fits between the middle ones");

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
      for (x = lip_segments, s = [-1, 1])
        translate([x - lip_len/2, s > 0 ? inner_hw - lip_gap - lip_t : -inner_hw + lip_gap, -lip_h])
          cube([lip_len, lip_t, lip_h]);
    }
    for (x = xs) translate([x, 0, -10]) cylinder(d=btn_hole, h=30);
    for (x = screw_x) translate([x, 0, 0]) {
      translate([0, 0, -10]) cylinder(d=screw_d, h=30);
      translate([0, 0, lid_t - 1.8]) cylinder(d1=screw_d, d2=csk_d, h=1.81);
    }
    if (show_labels) for (i = [0:3])
      translate([xs[i], -label_off, lid_t - label_depth]) linear_extrude(label_depth + 1)
        text(labels[i], size=label_size, font="DejaVu Sans:style=Bold", halign="center", valign="center");
  }
}

module board() {
  translate([-pcb_w/2, W/2 - wall - pcb_l, floor_t + cradle_h]) {
    color("#1f2a44") cube([pcb_w, pcb_l, pcb_t]);
    color("silver") translate([pcb_w/2 - 4.5, pcb_l - 7.3, pcb_t]) cube([9, 7.5, 3.2]);
  }
}
module button() {
  color("#c9ced3") translate([0, 0, lid_t]) cylinder(d=btn_head, h=2.5);
  color("#e8ebee") translate([0, 0, lid_t + 2.5]) cylinder(d=11, h=0.8);
  color("#9aa1a8") translate([0, 0, -btn_depth]) cylinder(d=16, h=btn_depth);
  color("#b0b6bc") translate([0, 0, -2.9]) cylinder(d=btn_nut / cos(30), h=2.9, $fn=6);
}

if (part == "base") base();
else if (part == "lid") lid();
else {
  color("#3a3f45") base();
  board();
  translate([0, 0, base_h]) {
    color("#d9dde0", 0.9) lid();
    for (x = xs) translate([x, 0, 0]) button();
  }
}

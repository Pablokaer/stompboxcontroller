// JME STOMPBOX footswitch controller – Raspberry Pi Zero W enclosure
// Two parts: BASE (tub) and LID (switch plate). Units: mm.
// Render one part:  openscad -D 'part="base"' -o base.stl case.scad
//                   openscad -D 'part="lid"'  -o lid.stl  case.scad
part = "assembly";            // "base" | "lid" | "assembly" (preview with Pi + switches)

// ---------- main dimensions ----------
L = 170;                      // outer length (max 170 per requirement)
W = 120;                      // outer width
R = 6;                        // outer corner radius
wall = 3;                     // side wall thickness
floor_t = 3;                  // base floor thickness
inner_h = 34;                 // clear height floor-top -> lid underside
lid_t = 4;                    // lid plate thickness (M12 switch thread needs <= ~6)
base_h = floor_t + inner_h;   // 37
lip_t = 2; lip_h = 3; lip_gap = 0.4;   // alignment lip under lid

// ---------- footswitches (12 mm soft-touch SPST) ----------
sw_hole = 12.4;               // M12 thread + clearance
pitch_x = 55;  row_y = 28;
switches = [                  // [x, y, label]
  [-pitch_x,  row_y, "MOD"], [0, row_y, "DELAY"], [pitch_x, row_y, "REVERB"],
  [-pitch_x/2, -row_y, "EQ"], [pitch_x/2, -row_y, "GATE"] ];
show_labels = true;           // false = plain lid with holes only
label_size = 5.5; label_depth = 0.8; label_off = 14;   // labels sit in front of each switch (player side = -Y)

// ---------- screws (M3) ----------
boss_d = 9; insert_d = 4.0;   // M3 heat-set insert (or 2.5 for self-tapping)
screw_d = 3.4; csk_d = 6.6;
boss_pos = [ for (sx=[-1,1], sy=[-1,1]) [sx*(L/2-wall-4.5), sy*(W/2-wall-4.5)] ];

// ---------- Raspberry Pi Zero W ----------
pi_L = 65; pi_W = 30; pi_hole_dx = 58; pi_hole_dy = 23; pi_x = 0; pi_y = 0;
standoff_h = 5; standoff_d = 6; pi_screw = 2.2;   // M2.5 self-tapping

// ---------- cable notch (micro-USB power cable) in back wall ----------
notch_x = pitch_x/2; notch_w = 7; notch_depth = 12;

$fn = 64;

module rbox(l, w, h, r) {
  linear_extrude(h) offset(r) square([l-2*r, w-2*r], center=true);
}

module base() {
  difference() {
    union() {
      difference() {
        rbox(L, W, base_h, R);
        translate([0,0,floor_t]) rbox(L-2*wall, W-2*wall, base_h, max(R-wall,1));
      }
      // corner screw bosses merged into the walls
      for (p = boss_pos) hull() {
        translate([p[0],p[1],0]) cylinder(d=boss_d, h=base_h);
        translate([sign(p[0])*(L/2-wall-1), sign(p[1])*(W/2-wall-1), 0]) cylinder(d=2, h=base_h);
      }
      // Pi standoffs
      for (sx=[-1,1], sy=[-1,1])
        translate([pi_x+sx*pi_hole_dx/2, pi_y+sy*pi_hole_dy/2, 0]) cylinder(d=standoff_d, h=floor_t+standoff_h);
    }
    for (p = boss_pos) translate([p[0],p[1],base_h-10]) cylinder(d=insert_d, h=11);
    for (sx=[-1,1], sy=[-1,1])
      translate([pi_x+sx*pi_hole_dx/2, pi_y+sy*pi_hole_dy/2, 1.5]) cylinder(d=pi_screw, h=20);
    // cable notch, open at the top (lid closes it)
    translate([notch_x-notch_w/2, W/2-wall-1, base_h-notch_depth]) cube([notch_w, wall+2, notch_depth+1]);
    translate([notch_x, W/2+1, base_h-notch_depth]) rotate([90,0,0]) cylinder(d=notch_w, h=wall+2);
    // floor vents under the Pi
    for (i=[-2:2]) translate([pi_x+i*10-1.25, pi_y-9, -1]) cube([2.5, 18, floor_t+2]);
    // rubber-foot recesses
    for (sx=[-1,1], sy=[-1,1]) translate([sx*(L/2-16), sy*(W/2-16), -0.01]) cylinder(d=12.5, h=1);
  }
}

module lid() {
  difference() {
    union() {
      rbox(L, W, lid_t, R);
      // alignment lip (hangs down inside the base walls)
      translate([0,0,-lip_h]) difference() {
        rbox(L-2*wall-2*lip_gap, W-2*wall-2*lip_gap, lip_h, max(R-wall-lip_gap,1));
        translate([0,0,-1]) rbox(L-2*wall-2*lip_gap-2*lip_t, W-2*wall-2*lip_gap-2*lip_t, lip_h+2, 1);
      }
      // stiffening rib along the middle (between switch rows, above the Pi)
      translate([-(L-2*wall-2*lip_gap)/2, -1.2, -6]) cube([L-2*wall-2*lip_gap, 2.4, 6]);
    }
    // clear the lip/rib around bosses and the cable notch
    for (p = boss_pos) translate([p[0],p[1],-10]) cylinder(d=boss_d+1, h=10);
    translate([notch_x-notch_w/2-1, W/2-wall-lip_t-2, -10]) cube([notch_w+2, lip_t+3, 10]);
    // switch holes
    for (s = switches) translate([s[0], s[1], -10]) cylinder(d=sw_hole, h=30);
    // countersunk M3 holes
    for (p = boss_pos) translate([p[0],p[1],0]) {
      translate([0,0,-10]) cylinder(d=screw_d, h=30);
      translate([0,0,lid_t-1.8]) cylinder(d1=screw_d, d2=csk_d, h=1.81);
    }
    // engraved labels
    if (show_labels) for (s = switches)
      translate([s[0], s[1] - label_off, lid_t-label_depth])
        linear_extrude(label_depth+1)
          text(s[2], size=label_size, font="DejaVu Sans:style=Bold", halign="center", valign="center");
  }
}

// ---------- preview helpers (not printed) ----------
module pi_zero() {
  color("#1f6b3a") translate([pi_x-pi_L/2, pi_y-pi_W/2, floor_t+standoff_h]) {
    difference() { cube([pi_L, pi_W, 1.6]); for (sx=[3.5,61.5], sy=[3.5,26.5]) translate([sx,sy,-1]) cylinder(d=2.75,h=4); }
    color("#222") translate([7, pi_W-5.5, 1.6]) cube([51, 5, 8.5]);   // GPIO header
    color("silver") for (x=[41.4, 54]) translate([x-4, -1, 1.6]) cube([8, 5, 3]); // micro-USB
  }
}
module footswitch() {
  color("silver") translate([0,0,-24]) cylinder(d=12, h=24 + lid_t + 8);
  color("#333") translate([0,0,lid_t+3]) cylinder(d=15, h=8);
  color("silver") translate([0,0,lid_t]) cylinder(d=16, h=3, $fn=6);
}

if (part == "base") base();
else if (part == "lid") lid();
else {
  color("#3a3f45") base();
  pi_zero();
  translate([0,0,base_h]) {
    color("#d9dde0", 0.92) lid();
    for (s = switches) translate([s[0], s[1], 0]) footswitch();
  }
}

# Solve the J40 fixed-dissection LP for every configured metal layer.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

set svg_file [make_result_file fixed_dissection_lp.svg]
puts "sliding-window density report (existing and LP post-fill):"
set planned_fill [fixed_dissection_lp \
  -rules "$script_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 2 \
  -max_density 0.75 \
  -svg $svg_file]

if {$planned_fill < 0.0} {
  error "Fixed-dissection LP returned a negative fill area."
}
if { ![file exists "${svg_file}_met1_tile_density.svg"] } {
  error "Fixed-dissection LP did not write the tile density map."
}
if { ![file exists "${svg_file}_met1_window_density.svg"] } {
  error "Fixed-dissection LP did not write the window density map."
}
set svg [open "${svg_file}_met1_window_density.svg" r]
set svg_contents [read $svg]
close $svg
if { ![string match "*metal=*" $svg_contents] } {
  error "Fixed-dissection LP SVG does not contain window metal densities."
}
if { ![string match "*lp=*" $svg_contents] } {
  error "Fixed-dissection LP SVG does not contain post-fill window densities."
}
puts "planned_fill_area=$planned_fill DBU^2"
puts "pass"

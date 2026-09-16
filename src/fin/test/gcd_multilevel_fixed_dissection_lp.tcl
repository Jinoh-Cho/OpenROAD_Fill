# Run J40 multilevel density analysis before solving the retained-window LP.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

set svg_file [make_result_file multilevel_fixed_dissection_lp.svg]
set planned_fill [multilevel_fixed_dissection_lp \
  -rules "$script_dir/fill.json" \
  -window 50 \
  -origin {0 0} \
  -resolution 4 \
  -accuracy 0.10 \
  -max_density 0.75 \
  -svg $svg_file]

if {$planned_fill < 0.0} {
  error "Multilevel fixed-dissection LP returned a negative fill area."
}
if { ![file exists "${svg_file}_met1.svg"] } {
  error "Multilevel fixed-dissection LP did not write an analysis SVG."
}
puts "planned_fill_area=$planned_fill DBU^2"
puts "pass"

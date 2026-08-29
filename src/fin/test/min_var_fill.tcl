# Exercise the MinVar fill Tcl command on the standard FIN test design.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

min_var_fill -rules "$script_dir/fill.json"

puts "pass"

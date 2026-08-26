# Verify total metal area is invariant under TileGrid dissection and origin.
set script_dir [file dirname [file normalize [info script]]]
source "$script_dir/helpers.tcl"

read_lef "$script_dir/sky130hd/sky130hd.tlef"
read_lef "$script_dir/sky130hd/sky130_fd_sc_hd_merged.lef"
read_def "$script_dir/gcd_prefill.def"

set rules "$script_dir/fill.json"

set svg_file_0 [make_result_file tile_grid_50um_r2_0_0.svg]
set svg_file_1 [make_result_file tile_grid_50um_r4_0_0.svg]
set svg_file_2 [make_result_file tile_grid_50um_r4_10_10.svg]
set svg_file_3 [make_result_file tile_grid_80um_r2_0_0.svg]

set area_50_origin_r2 [tile_grid_metal_area -rules $rules -window 50 -origin {0 0} -resolution 2 -svg $svg_file_0]
set area_50_origin_r4 [tile_grid_metal_area -rules $rules -window 50 -origin {0 0} -resolution 4 -svg $svg_file_1]
set area_50_offset_r4 [tile_grid_metal_area -rules $rules -window 50 -origin {10 10} -resolution 4 -svg $svg_file_2]
set area_80_origin_r2 [tile_grid_metal_area -rules $rules -window 80 -origin {0 0} -resolution 2 -svg $svg_file_3]

set reference_area $area_50_origin_r2
puts "tile grid results:"
foreach {name area svg_file} [list \
  "50um, r=2, origin=0,0" $area_50_origin_r2 $svg_file_0 \
  "50um, r=4, origin=0,0" $area_50_origin_r4 $svg_file_1 \
  "50um, r=4, origin=10,10" $area_50_offset_r4 $svg_file_2 \
  "80um, r=2, origin=0,0" $area_80_origin_r2 $svg_file_3] {
  puts "  $name: metal_area=$area DBU^2, svg=$svg_file"
  set difference [expr {abs($reference_area - $area)}]
  if {$difference > 0.5} {
    error "Tile-grid dissection changed total metal area: $name differs by $difference DBU^2."
  }
}
puts "pass"

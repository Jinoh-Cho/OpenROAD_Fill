# Compare actual M2 geometry after existing density fill and LP fill methods.
# Environment and inputs are frozen by run_m2_fill_quality_comparison.py.
foreach name {QUALITY_INPUT QUALITY_LEFS QUALITY_RULES QUALITY_OUTPUT QUALITY_METHOD QUALITY_LAYER} {
  if {![info exists ::env($name)]} {error "$name is required"}
}
set output $::env(QUALITY_OUTPUT)
set method $::env(QUALITY_METHOD)
if {$method ni {density min_amount lp_minvar}} {error "Unknown method"}
file mkdir $output
set load_start [clock milliseconds]
foreach lef $::env(QUALITY_LEFS) {read_lef $lef}
read_def $::env(QUALITY_INPUT)
set load_seconds [expr {([clock milliseconds]-$load_start)/1000.0}]
set rules $::env(QUALITY_RULES)
set region [ord::get_db_core]
set db [ord::get_db]
set tech [$db getTech]
set dbu [$tech getDbUnitsPerMicron]
set bounds [list [$region xMin] [$region yMin] [$region xMax] [$region yMax]]
set common [list -rules $rules -window 50 -resolution 4 -origin {0 0} \
  -min_window_density 0.3 -max_density 0.6]
set before_start [clock milliseconds]
set before_area [tile_grid_metal_area {*}$common -density_report "$output/before_density.json"]
set before_seconds [expr {([clock milliseconds]-$before_start)/1000.0}]
set status ok
set lp_area null
set fill_start [clock milliseconds]
set code [catch {
  if {$method eq "density"} {
    density_fill -rules $rules
  } else {
    set options [list -rules $rules -window 50 -resolution 4 -origin {0 0} \
      -min_tile_density 0.2 -max_tile_density 1.0 \
      -min_window_density 0.3 -max_window_density 0.6 \
      -lp_solver pdlp -lp_threads 16 -candidate_threads 32 -placement_threads 32 \
      -selection_policy greedy -verify_post_fill_density 1 \
      -lp_diagnostics "$output/lp_failure" -density_report "$output/lp_fill_report.json"]
    if {$method eq "min_amount"} {
      set lp_area [fixed_dissection_lp_min_amount_fill {*}$options]
    } else {
      set lp_area [fixed_dissection_lp_min_var_fill {*}$options]
    }
  }
} message]
set fill_seconds [expr {([clock milliseconds]-$fill_start)/1000.0}]
if {!$code && $method ne "density" && [file exists "$output/lp_fill_report.json"]} {
  set report [open "$output/lp_fill_report.json" r]
  set lp_report [read $report]
  close $report
  if {[regexp {"solved": false} $lp_report]} {
    set code 1
    set status infeasible
    set message "LP did not solve; inspect lp_fill_report.json and lp_failure diagnostics."
  }
}
if {$code} {
  if {$status ne "infeasible"} {set status failed}
  set error_file [open "$output/failure.txt" w]
  puts $error_file $message
  close $error_file
  puts "QUALITY_FAILURE: $method $message"
}
set analysis_start [clock milliseconds]
set after_area [tile_grid_metal_area {*}$common -density_report "$output/after_density.json"]
set analysis_seconds [expr {([clock milliseconds]-$analysis_start)/1000.0}]
set counts [fin::layout_feature_counts_cmd]
set counts_file [open "$output/features.json" w]
puts $counts_file $counts
close $counts_file
set result [open "$output/result.json" w]
puts $result "{\"method\":\"$method\",\"status\":\"$status\",\"dbu\":$dbu,\"region_dbu\":\[[join $bounds ,]\],\"before_metal_area_dbu2\":$before_area,\"after_metal_area_dbu2\":$after_area,\"added_union_area_dbu2\":[expr {$after_area-$before_area}],\"lp_placed_fill_area_dbu2\":$lp_area,\"load_seconds\":$load_seconds,\"before_analysis_seconds\":$before_seconds,\"fill_seconds\":$fill_seconds,\"after_analysis_seconds\":$analysis_seconds}"
close $result
puts "QUALITY_RESULT: method=$method status=$status added_area=[expr {$after_area-$before_area}] DBU^2"
if {$code} {exit 1}
exit

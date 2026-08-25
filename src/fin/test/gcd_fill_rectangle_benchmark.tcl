# Verify the experimental rectangle-extraction benchmark command.
source helpers.tcl

read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130_fd_sc_hd_merged.lef
read_def gcd_prefill.def

# The command only measures get_rectangles() after all setup is complete.
set left_copies 0
set right_copies 0
set benchmark_runs 1

if { [info exists ::env(FIN_BENCH_LEFT)] } {
  set left_copies $::env(FIN_BENCH_LEFT)
}
if { [info exists ::env(FIN_BENCH_RIGHT)] } {
  set right_copies $::env(FIN_BENCH_RIGHT)
}
if { [info exists ::env(FIN_BENCH_RUNS)] } {
  set benchmark_runs $::env(FIN_BENCH_RUNS)
}
density_fill_rectangle_extraction_benchmark \
  -rules fill.json \
  -left $left_copies \
  -right $right_copies \
  -runs $benchmark_runs

puts "pass"

# Check lp_fill option marshaling without running a solver.
rename fin::lp_fill_cmd fin::lp_fill_cmd_original
proc fin::lp_fill_cmd { args } {
  set ::lp_fill_arguments $args
}

lp_fill -rules fill.json -objective min_fill \
  -solver pdlp -solver_threads 16 -num_threads 8 \
  -window_size {50 30} -window_step {13 11} \
  -min_density 0.2 -max_density 0.7 \
  -min_tile_density 0.1 -max_tile_density 0.6
set expected {fill.json min_fill pdlp 16 8 50 30 13 11 0.2 0.7 0.1 0.6}
if { $::lp_fill_arguments ne $expected } {
  error "Unexpected lp_fill arguments: $::lp_fill_arguments"
}

lp_fill -rules fill.json -objective min_var \
  -window_size {50 50} -window_step {12.5 12.5}
set expected {fill.json min_var glop 1 1 50 50 12.5 12.5 0.20 0.70 0.0 1.0}
if { $::lp_fill_arguments ne $expected } {
  error "Unexpected lp_fill defaults: $::lp_fill_arguments"
}

foreach arguments {
  {-rules fill.json -objective min_fill -window_size {50 50}
    -window_step {10 10} -placement_strategy sequential}
  {-rules fill.json -objective min_fill -window_size {50 50}
    -window_step {10 10} -placement_strategy uniform}
  {-objective min_fill -window_size {50 50} -window_step {10 10}}
  {-rules fill.json -objective min_fill -window_size {50} -window_step {10 10}}
  {-rules fill.json -objective min_fill -window_size {50 50} -window_step {10 10} extra}
} {
  if { ![catch { lp_fill {*}$arguments }] } {
    error "Invalid lp_fill arguments were accepted: $arguments"
  }
}
rename fin::lp_fill_cmd {}
rename fin::lp_fill_cmd_original fin::lp_fill_cmd
if {
  ![catch {
    lp_fill -rules fill.json -objective min_fill \
      -window_size {50 50} -window_step {10 10}
  } message] || ![string match "*FIN-0011*" $message]
} {
  error "lp_fill did not reject a missing design: $message"
}
puts "pass"

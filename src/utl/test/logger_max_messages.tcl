source "helpers.tcl"

# Exercise output beyond the former 1000-message cap without a million-line log.
for { set i 0 } { $i < 2000 } { incr i } { utl::warn UTL 1234 "test" }

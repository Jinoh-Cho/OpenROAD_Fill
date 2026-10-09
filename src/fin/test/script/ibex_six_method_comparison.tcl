# Run the common six-method worker with the IBEX prefill layout.
set ::env(FIN_BENCHMARK) ibex
source [file join [file dirname [file normalize [info script]]] gcd_six_method_comparison.tcl]

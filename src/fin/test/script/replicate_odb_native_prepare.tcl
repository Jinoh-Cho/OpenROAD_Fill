# Only load/save bridges. All replication is performed by the C++ executable.
if {[catch {
  if {$::env(ODB_NATIVE_MODE) eq "export"} {
    read_db $::env(ODB_NATIVE_INPUT)
    write_def $::env(ODB_NATIVE_OUTPUT)
  } else {
    for {set i 0} {$i < $::env(ODB_NATIVE_LEF_COUNT)} {incr i} {
      read_lef $::env(ODB_NATIVE_LEF_$i)
    }
    read_def $::env(ODB_NATIVE_INPUT)
    write_db $::env(ODB_NATIVE_OUTPUT)
  }
} message options]} {
  puts stderr $message
  puts stderr [dict get $options -errorinfo]
  exit 1
}
exit 0

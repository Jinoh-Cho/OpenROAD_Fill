# Internal worker for replicate_odb.py. All copies stay in the source flat block.
namespace eval odb_replicate {}

proc odb_replicate::nonnull {value description} {
  if {$value eq "NULL" || $value eq ""} {error "Failed to create $description"}
  return $value
}

proc odb_replicate::shift {x delta} {
  set value [expr {wide($x) + $delta}]
  if {$value < -2147483648 || $value > 2147483647} {
    error "Translated coordinate exceeds signed 32-bit OpenDB range"
  }
  return [format %d $value]
}

proc odb_replicate::box_record {box} {
  if {[$box getLayerMask] != 0} {error "Colored routing/pin boxes are unsupported by the current Tcl API"}
  return [list [$box getTechLayer] [$box xMin] [$box yMin] [$box xMax] [$box yMax] [$box getLayerMask]]
}

proc odb_replicate::compile_wire {wire} {
  if {$wire eq "NULL"} {return {}}
  # RECT is not an OUTPUT typemap in the existing Tcl bindings. Extract its
  # relative operands directly; the wire codec stores four consecutive values.
  set rects {}; set length [$wire length]
  for {set i 0} {$i < $length} {incr i} {
    set op [expr {[$wire getOpcode $i] & 31}]
    if {$op == 14} {
      lappend rects [list [$wire getData $i] [$wire getData [expr {$i+1}]] \
          [$wire getData [expr {$i+2}]] [$wire getData [expr {$i+3}]]]
      incr i 3
    } elseif {$op in {16 17} && [$wire getData $i] != 0} {
      error "Signal-wire mask coloring is not supported by the current Tcl API"
    } elseif {$op == 12 && [$wire getData $i] != 0} {
      error "Nonzero wire-point properties are unsupported by this Tcl replicator"
    }
  }
  set plan {}; set ri 0
  # A fresh decoder prevents mask state leaking between independent nets.
  set decoder [odb::dbWireDecoder]
  $decoder begin $wire
  while {1} {
    set op [$decoder next]
    if {$op == 12} {break}
    switch $op {
      0 - 1 - 2 - 3 {
        set layer [$decoder getLayer]; set type [$decoder getWireType]
        set ref -1
        if {$op != 0} {set ref [$decoder getJunctionValue]}
        set rule NULL
        if {[$decoder peek] == 11} {$decoder next; set rule [$decoder getRule]}
        if {$op == 1} {
          set point_op [$decoder next]
          if {$point_op == 5} {
            set ext [lindex [$decoder getPoint_ext] 2]
          } elseif {$point_op == 4} {set ext NULL} else {error "Missing junction point"}
          lappend plan [list J [$decoder getJunctionId] $ref $type $rule $ext]
        } else {
          lappend plan [list P $op $ref $layer $type $rule]
        }
      }
      4 - 5 {
        if {$op == 5} {set point [$decoder getPoint_ext]} else {set point [$decoder getPoint]}
        # Point properties are not reliably exposed by the current Tcl overloads.
        lappend plan [list X [$decoder getJunctionId] $point 0 [$decoder getColor]]
      }
      6 - 7 {
        if {$op == 6} {set via [$decoder getVia]} else {set via [$decoder getTechVia]}
        lappend plan [list V $op [$decoder getJunctionId] $via]
      }
      8 {lappend plan [list R [lindex $rects $ri]]; incr ri}
      9 {
        set term [$decoder getITerm]
        lappend plan [list I [[$term getInst] getId] [[$term getMTerm] getName]]
      }
      10 {lappend plan [list B [[$decoder getBTerm] getId]]}
      default {error "Unsupported wire decoder opcode $op"}
    }
  }
  # The Tcl constructor registers an owning object command. The raw delete_*
  # wrapper frees only C++ storage and leaves that command alive; allocating a
  # decoder at the same address then deletes the stale command and frees twice.
  # -delete removes the command and destroys its C++ object exactly once.
  $decoder -delete
  return $plan
}

proc odb_replicate::copy_wire {net plan dx dy inst_map pin_map encoder} {
  if {[llength $plan] == 0} {return}
  set wire [nonnull [odb::dbWire_create $net] wire]
  $encoder begin $wire
  set junctions [dict create]
  set last_color NULL
  foreach command $plan {
    switch [lindex $command 0] {
      P {
        lassign $command tag op ref layer type rule
        if {$op == 0} {set args [list $layer $type]; set method newPath} else {
          set args [list [dict get $junctions $ref] $layer $type]
          set method [expr {$op == 2 ? "newPathShort" : "newPathVirtualWire"}]
        }
        if {$rule ne "NULL"} {lappend args $rule}
        $encoder $method {*}$args
      }
      J {
        lassign $command tag id ref type rule ext
        set args [list [dict get $junctions $ref]]
        if {$ext ne "NULL"} {lappend args $ext; set method newPathExt} else {set method newPath}
        lappend args $type
        if {$rule ne "NULL"} {lappend args $rule}
        $encoder $method {*}$args
        dict set junctions $id [dict get $junctions $ref]
      }
      X {
        lassign $command tag id point property color
        if {$color ne $last_color} {
          if {$color eq "NULL"} {$encoder clearColor} else {$encoder setColor $color}
          set last_color $color
        }
        lassign $point x y ext
        set x [shift $x $dx]; set y [shift $y $dy]
        if {[llength $point] == 3} {
          set new_id [$encoder addPoint $x $y $ext]
        } else {set new_id [$encoder addPoint $x $y]}
        dict set junctions $id $new_id
      }
      V {
        lassign $command tag op id via
        if {$op == 6} {set new_id [$encoder addVia $via]} else {set new_id [$encoder addTechVia $via]}
        dict set junctions $id $new_id
      }
      R {$encoder addRect {*}[lindex $command 1]}
      I {
        lassign $command tag inst_id term
        $encoder addITerm [[dict get $inst_map $inst_id] findITerm $term]
      }
      B {$encoder addBTerm [dict get $pin_map [lindex $command 1]]}
    }
  }
  $encoder end
}

proc odb_replicate::compile_swires {net} {
  set plans {}
  foreach wire [$net getSWires] {
    set boxes {}
    foreach box [$wire getWires] {
      if {[$box isVia]} {
        set via [$box getTechVia]; set type T
        if {$via eq "NULL"} {set via [$box getBlockVia]; set type V}
        set masks {}
        if {[$box hasViaLayerMasks]} {
          error "Special-wire via masks are unsupported by the current Tcl API"
        }
        lappend boxes [list $type $via [$box getViaXY] [$box getWireShapeType] $masks]
      } else {
        if {[$box getDirection] == 3} {error "Octilinear special wires are unsupported"}
        lappend boxes [list R [box_record $box] [$box getWireShapeType] [$box getDirection]]
      }
    }
    set shield [$wire getShield]
    if {$shield ne "NULL"} {set shield [$shield getId]}
    lappend plans [list [$wire getWireType] $shield $boxes]
  }
  return $plans
}

proc odb_replicate::copy_swires {net plans dx dy net_map} {
  foreach plan $plans {
    lassign $plan type shield boxes
    if {$shield ne "NULL"} {set shield [dict get $net_map $shield]}
    set wire [nonnull [odb::dbSWire_create $net $type $shield] special-wire]
    foreach item $boxes {
      lassign $item tag data shape extra masks
      if {$tag eq "R"} {
        lassign $data layer x0 y0 x1 y1 mask
        set box [odb::dbSBox_create $wire $layer [shift $x0 $dx] [shift $y0 $dy] \
            [shift $x1 $dx] [shift $y1 $dy] $shape $extra]
        nonnull $box special-box
        if {$mask} {$box setLayerMask $mask}
      } else {
        lassign $shape x y
        set box [odb::dbSBox_create $wire $data [shift $x $dx] [shift $y $dy] $extra]
        nonnull $box special-via
        if {[llength $masks]} {$box setViaLayerMask {*}$masks}
      }
    }
  }
}

proc odb_replicate::run {} {
  global env
  set start [clock milliseconds]
  if {[file extension $env(ODB_REPLICATE_INPUT)] eq ".odb"} {
    read_db $env(ODB_REPLICATE_INPUT)
  } else {
    for {set i 0} {$i < $env(ODB_REPLICATE_LEF_COUNT)} {incr i} {read_lef $env(ODB_REPLICATE_LEF_$i)}
    read_def $env(ODB_REPLICATE_INPUT)
  }
  set loaded [clock milliseconds]
  set block [ord::get_db_block]
  # Only flat, unfilled benchmark blocks are supported. Reject instead of silently
  # leaving auxiliary floorplan constraints unreplicated.
  foreach method {getChildren getRegions getGroups getBlockages getObstructions getFills} {
    if {[llength [$block $method]]} {error "Source $method must be empty; unsupported replication input"}
  }
  set nx $env(ODB_REPLICATE_NX); set ny $env(ODB_REPLICATE_NY)
  set die [$block getDieArea]
  set px [expr {[$die dx]+$env(ODB_REPLICATE_GAP_X)}]
  set py [expr {[$die dy]+$env(ODB_REPLICATE_GAP_Y)}]
  set max_x [shift [$die xMax] [expr {($nx-1)*$px}]]
  set max_y [shift [$die yMax] [expr {($ny-1)*$py}]]
  set instances {}; set rows {}; set pins {}; set nets {}; set grids {}
  foreach inst [$block getInsts] {
    lappend instances [list [$inst getId] [$inst getName] [$inst getMaster] [$inst getOrigin] \
        [$inst getOrient] [$inst getPlacementStatus] [$inst isPhysicalOnly] [$inst getSourceType] [$inst getWeight]]
  }
  foreach row [$block getRows] {
    lappend rows [list [$row getName] [$row getSite] [$row getOrigin] [$row getOrient] \
        [$row getDirection] [$row getSiteCount] [$row getSpacing]]
  }
  foreach pin [$block getBTerms] {
    set bpins {}
    foreach bpin [$pin getBPins] {
      set boxes {}
      foreach box [$bpin getBoxes] {
        if {[$box isVia]} {error "BPin vias are unsupported"}
        lappend boxes [box_record $box]
      }
      lappend bpins [list [$bpin getPlacementStatus] $boxes]
    }
    lappend pins [list [$pin getId] [$pin getName] [[$pin getNet] getId] \
        [$pin getIoType] [$pin getSigType] [$pin isSpecial] $bpins]
  }
  set ni 0
  foreach net [$block getNets] {
    set terms {}
    foreach term [$net getITerms] {
      lappend terms [list [[$term getInst] getId] [[$term getMTerm] getName] [$term isSpecial]]
    }
    set shared [expr {$env(ODB_REPLICATE_SHARE) && [$net isSpecial] && [$net getSigType] in {POWER GROUND}}]
    if {!$shared && [$net isWildConnected]} {
      # The source has already resolved '*'. Preserve those explicit connections
      # and prevent an optional DEF round trip from reconnecting every copy.
      $net clearWildConnected
    }
    lappend nets [list [$net getId] [$net getName] [$net getSigType] [$net isSpecial] \
        [$net getWeight] [$net getSourceType] [$net getNonDefaultRule] $shared $terms \
        [compile_wire [$net getWire]] [compile_swires $net]]
    incr ni
    if {$ni % 10000 == 0} {puts "ODB template nets: $ni"; flush stdout}
  }
  # Snapshot grid patterns before appending translated patterns.
  set track_grids [$block getTrackGrids]
  set gc [$block getGCellGrid]
  if {$gc ne "NULL"} {lappend track_grids $gc}
  foreach grid $track_grids {
    foreach axis {X Y} {
      set patterns {}
      for {set i 0} {$i < [$grid getNumGridPatterns$axis]} {incr i} {
        lappend patterns [$grid getGridPattern$axis $i]
      }
      lappend grids [list $grid $axis $patterns]
    }
  }
  set prepared [clock milliseconds]
  puts "ODB loaded once; templates ready. Copies=$nx x $ny"
  set encoder [odb::dbWireEncoder]
  set count [expr {$nx*$ny}]
  for {set tile 1} {$tile < $count} {incr tile} {
    set col [expr {$tile % $nx}]; set row [expr {$tile / $nx}]
    set dx [expr {$col*$px}]; set dy [expr {$row*$py}]
    set prefix "t${col}_${row}__"
    set inst_map [dict create]; set net_map [dict create]; set pin_map [dict create]
    foreach record $instances {
      lassign $record id name master origin orient status physical source weight
      set inst [nonnull [odb::dbInst_create $block $master "$prefix$name" $physical] instance]
      $inst setOrient $orient
      lassign $origin x y
      $inst setOrigin [shift $x $dx] [shift $y $dy]
      $inst setPlacementStatus $status
      $inst setSourceType $source; $inst setWeight $weight
      dict set inst_map $id $inst
    }
    foreach record $nets {
      lassign $record id name signal special weight source rule shared
      if {$shared} {set net [$block findNet $name]} else {
        set net [nonnull [odb::dbNet_create $block "$prefix$name"] net]
        $net setSigType $signal; $net setWeight $weight; $net setSourceType $source
        if {$special} {$net setSpecial}
        if {$rule ne "NULL"} {$net setNonDefaultRule $rule}
      }
      dict set net_map $id $net
    }
    foreach record $pins {
      lassign $record id name net_id io signal special bpins
      set pin [nonnull [odb::dbBTerm_create [dict get $net_map $net_id] "$prefix$name"] bterm]
      $pin setIoType $io; $pin setSigType $signal
      if {$special} {$pin setSpecial}
      foreach item $bpins {
        lassign $item status boxes
        set bpin [nonnull [odb::dbBPin_create $pin] bpin]
        $bpin setPlacementStatus $status
        foreach data $boxes {
          lassign $data layer x0 y0 x1 y1 mask
          set box [odb::dbBox_create $bpin $layer [shift $x0 $dx] [shift $y0 $dy] \
              [shift $x1 $dx] [shift $y1 $dy]]
          nonnull $box bpin-box
          if {$mask} {$box setLayerMask $mask}
        }
      }
      dict set pin_map $id $pin
    }
    foreach record $nets {
      lassign $record id name signal special weight source rule shared terms wire swires
      set net [dict get $net_map $id]
      foreach term $terms {
        lassign $term inst_id terminal is_special
        set iterm [[dict get $inst_map $inst_id] findITerm $terminal]
        $iterm connect $net
        if {$is_special} {$iterm setSpecial}
      }
      copy_wire $net $wire $dx $dy $inst_map $pin_map $encoder
      copy_swires $net $swires $dx $dy $net_map
    }
    foreach record $rows {
      lassign $record name site origin orient direction sites spacing
      lassign $origin x y
      nonnull [odb::dbRow_create $block "$prefix$name" $site [shift $x $dx] \
          [shift $y $dy] $orient $direction $sites $spacing] row
    }
    puts [format "ODB replicate: %d/%d tiles (%.1f%%), elapsed %.1fs" \
        [expr {$tile+1}] $count [expr {100.0*($tile+1)/$count}] \
        [expr {([clock milliseconds]-$prepared)/1000.0}]]
    flush stdout
  }
  $encoder -delete
  foreach record $grids {
    lassign $record grid axis patterns
    set limit [expr {$axis eq "X" ? $nx : $ny}]
    set pitch [expr {$axis eq "X" ? $px : $py}]
    for {set i 1} {$i < $limit} {incr i} {
      foreach pattern $patterns {
        lassign $pattern origin lines step
        $grid addGridPattern$axis [shift $origin [expr {$i*$pitch}]] $lines $step {*}[lrange $pattern 3 end]
      }
    }
  }
  set new_die [odb::Rect]
  $new_die init [$die xMin] [$die yMin] $max_x $max_y
  $block setDieArea $new_die
  $new_die -delete
  # OpenDB caches core area separately; adding rows does not invalidate it.
  # Recompute the expanded placement region before any fill/density operation.
  $block setCoreArea [$block computeCoreArea]
  set replicated [clock milliseconds]
  puts "ODB replication complete; saving $env(ODB_REPLICATE_OUTPUT)"
  write_db $env(ODB_REPLICATE_OUTPUT)
  set saved [clock milliseconds]
  if {$env(ODB_REPLICATE_DEF) ne ""} {write_def $env(ODB_REPLICATE_DEF)}
  set shared_count 0
  foreach record $nets {incr shared_count [lindex $record 7]}
  set stats [open $env(ODB_REPLICATE_STATS) w]
  foreach {key value} [list load_seconds [expr {($loaded-$start)/1000.0}] \
      template_seconds [expr {($prepared-$loaded)/1000.0}] \
      replication_seconds [expr {($replicated-$prepared)/1000.0}] \
      write_odb_seconds [expr {($saved-$replicated)/1000.0}] \
      instances [expr {[llength $instances]*wide($count)}] \
      nets [expr {([llength $nets]-$shared_count)*wide($count)+$shared_count}] \
      bterms [expr {[llength $pins]*wide($count)}] \
      rows [expr {[llength $rows]*wide($count)}] die_xmax $max_x die_ymax $max_y] {
    puts $stats "$key=$value"
    puts "$key=$value"
  }
  close $stats
}

if {[catch {odb_replicate::run} message options]} {
  puts stderr "ODB replication failed: $message"
  puts stderr [dict get $options -errorinfo]
  exit 1
}
exit 0

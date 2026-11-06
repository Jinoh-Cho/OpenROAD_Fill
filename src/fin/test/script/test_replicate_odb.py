#!/usr/bin/env python3
"""Small native OpenDB integration checks; no Python odb module required."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import replicate_odb as replicator


class ReplicateODBTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.openroad = Path(os.environ.get('OPENROAD_BIN', str(replicator.REPO / 'bazel-bin/openroad')))
        cls.temporary = tempfile.TemporaryDirectory(prefix='test_replicate_odb_')
        cls.root = Path(cls.temporary.name)
        lef = '''VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.2 ; WIDTH 0.1 ; SPACING 0.1 ; END M1
LAYER V1 TYPE CUT ; SPACING 0.1 ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.2 ; WIDTH 0.1 ; SPACING 0.1 ; END M2
SITE site CLASS CORE ; SIZE 1 BY 1 ; SYMMETRY X Y ; END site
MACRO cell
 CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SYMMETRY X Y ; SITE site ;
'''
        for name, use in [('A', 'SIGNAL'), ('Y', 'SIGNAL'), ('VDD', 'POWER'), ('VSS', 'GROUND')]:
            lef += ' PIN %s DIRECTION INOUT ; USE %s ; PORT LAYER M1 ; RECT 0 0 0.1 0.1 ; END END %s\n' % (name, use, name)
        lef += 'END cell\nEND LIBRARY\n'
        (cls.root / 'source.lef').write_text(lef)
        (cls.root / 'source.def').write_text('''VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN mini ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( -1000 -2000 ) ( 5000 5000 ) ;
ROW row0 site 0 0 N DO 3 BY 1 STEP 1000 0 ;
TRACKS X 100 DO 20 STEP 200 LAYER M1 ;
GCELLGRID Y -1000 DO 10 STEP 500 ;
VIAS 1 ;
- via12 + RECT M1 ( -50 -50 ) ( 50 50 ) + RECT V1 ( -25 -25 ) ( 25 25 ) + RECT M2 ( -50 -50 ) ( 50 50 ) ;
END VIAS
COMPONENTS 2 ;
- u0 cell + PLACED ( 0 0 ) N ;
- u1 cell + PLACED ( 2000 1000 ) FN ;
END COMPONENTS
PINS 1 ;
- port + NET sig + DIRECTION INPUT + LAYER M2 ( -50 -50 ) ( 50 50 ) + PLACED ( 0 3000 ) N ;
END PINS
SPECIALNETS 2 ;
- VDD ( * VDD ) + USE POWER + ROUTED M1 100 ( 0 1000 ) ( 3000 * ) ;
- VSS ( * VSS ) + USE GROUND + ROUTED M1 100 ( 0 0 ) ( 3000 * ) ;
END SPECIALNETS
NETS 1 ;
- sig ( PIN port ) ( u0 A ) ( u1 Y ) + USE SIGNAL ;
END NETS
END DESIGN
''')
        # Build a source ODB with branches, extension lengths, a block via, a
        # relative RECT and pin opcodes. This exercises .odb input.
        (cls.root / 'fixture.tcl').write_text('''
read_lef $::env(ODB_TEST_ROOT)/source.lef
read_def $::env(ODB_TEST_ROOT)/source.def
set b [ord::get_db_block]
set n [$b findNet sig]
set e [odb::dbWireEncoder]
set w [odb::dbWire_create $n]
$e begin $w
$e newPath [[ord::get_db_tech] findLayer M1] ROUTED
set j0 [$e addPoint 0 0]
set j1 [$e addPoint 1000 0]
$e addVia [$b findVia via12]
$e addPoint 1000 1000 0
$e addRect -50 -50 50 50
$e addITerm [[$b findInst u1] findITerm Y]
$e newPath $j0 ROUTED
$e addPoint 0 1000
$e addBTerm [$b findBTerm port]
$e newPathExt $j1 20 ROUTED
$e addPoint 2000 0
$e end
write_db $::env(ODB_TEST_ROOT)/source.odb
exit
''')
        cls.run_tcl('fixture.tcl')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def run_tcl(cls, script, extra=None):
        env = os.environ.copy()
        env['ODB_TEST_ROOT'] = str(cls.root)
        env.update(extra or {})
        run = subprocess.run([str(cls.openroad), '-no_init', '-no_splash', '-exit', str(cls.root / script)],
                             env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if run.returncode:
            raise AssertionError(run.stdout.decode(errors='replace'))

    def replicate(self, source, output, shared):
        command = [sys.executable, str(Path(replicator.__file__)), str(self.root / source),
                   str(self.root / output), '--nx', '2', '--ny', '2',
                   '--gap-x', '100', '--gap-y', '200', '--openroad', str(self.openroad)]
        if shared:
            command.append('--share-power')
        if source.endswith('.def'):
            command += ['--lefs', str(self.root / 'source.lef')]
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(run.returncode, 0, run.stdout.decode(errors='replace'))

    def validate(self, filename, shared):
        # Canonical wire decoding ignores object IDs and normalizes coordinates
        # and instance names. The replica must retain the same opcode sequence.
        (self.root / 'check.tcl').write_text('''
read_db $::env(ODB_TEST_ROOT)/$::env(ODB_TEST_OUTPUT)
set b [ord::get_db_block]
if {[llength [$b getInsts]] != 8} {error "instance count"}
if {[llength [$b getBTerms]] != 4} {error "pin count"}
set count [expr {$::env(ODB_TEST_SHARED) ? 6 : 12}]
if {[llength [$b getNets]] != $count} {error "net count"}
set core [$b getCoreArea]
if {[$core xMax] <= 5000 || [$core yMax] <= 5000} {error "core area not expanded"}
set die [$b getDieArea]
if {[$die xMax] != 11100 || [$die yMax] != 12200} {error "die area"}
proc signature {net dx dy} {
 set d [odb::dbWireDecoder]
 set w [$net getWire]; $d begin $w
 set result {}
 while {1} {
  set op [$d next]
  if {$op == 12} {break}
  switch $op {
   0 {lappend result [list P [[$d getLayer] getName] [$d getWireType]]}
   1 - 2 - 3 {
    lassign [$w getCoord [$d getJunctionValue]] x y
    lappend result [list J $op [expr {$x-$dx}] [expr {$y-$dy}]]
   }
   4 - 5 {
    if {$op == 5} {set p [$d getPoint_ext]} else {set p [$d getPoint]}
    lassign $p x y ext
    lappend result [list X $op [expr {$x-$dx}] [expr {$y-$dy}] $ext [$d getColor]]
   }
   6 {lappend result [list V [[$d getVia] getName]]}
   7 {lappend result [list T [[$d getTechVia] getName]]}
   8 {lappend result R}
   9 {lappend result [list I [[ $d getITerm] getMTerm]]}
   10 {lappend result B}
   default {lappend result $op}
  }
 }
 $d -delete
 return $result
}
set expected [signature [$b findNet sig] 0 0]
foreach {col row} {1 0 0 1 1 1} {
 set prefix t${col}_${row}__
 set dx [expr {$col*6100}]; set dy [expr {$row*7200}]
 set n [$b findNet ${prefix}sig]
 if {[signature $n $dx $dy] ne $expected} {error "wire opcode/coordinate/color mismatch: $prefix"}
 foreach pin {A Y} {
  set inst [$b findInst ${prefix}u0]
  if {$pin eq "A" && [[$inst findITerm $pin] getNet] ne $n} {error "signal connectivity"}
 }
 foreach supply {VDD VSS} {
  set name [expr {$::env(ODB_TEST_SHARED) ? $supply : "$prefix$supply"}]
  if {[[$inst findITerm $supply] getNet] ne [$b findNet $name]} {error "power connectivity"}
 }
}
puts "REPLICATION_CHECK_PASS"
exit
''')
        self.run_tcl('check.tcl', {'ODB_TEST_OUTPUT': filename, 'ODB_TEST_SHARED': str(int(shared))})

    def test_odb_shared_power_with_wire_branches(self):
        self.replicate('source.odb', 'shared.odb', True)
        self.validate('shared.odb', True)

    def test_odb_independent_power_with_wire_branches(self):
        self.replicate('source.odb', 'separate.odb', False)
        self.validate('separate.odb', False)

    def test_def_input(self):
        self.replicate('source.def', 'from_def.odb', True)
        metadata = json.loads((self.root / 'from_def.odb.json').read_text())
        self.assertEqual(metadata['stats']['instances'], '8')

    def test_many_wire_decoders_do_not_leave_owned_tcl_commands(self):
        # Thousands of independently routed nets force repeated decoder allocation
        # and address reuse. Raw delete_dbWireDecoder left an owning Tcl command
        # behind and crashed on the actual TPU despite passing a tiny fixture.
        source = (self.root / 'source.def').read_text()
        source = source.replace('NETS 1 ;', 'NETS 5001 ;')
        extra = ''.join('- n%d + ROUTED M1 ( 0 0 ) ( 1000 0 ) ;\n' % i
                        for i in range(5000))
        source = source.replace('END NETS\n', extra + 'END NETS\n')
        (self.root / 'many.def').write_text(source)
        self.replicate('many.def', 'many.odb', True)
        metadata = json.loads((self.root / 'many.odb.json').read_text())
        self.assertEqual(metadata['stats']['nets'], str(5001 * 4 + 2))

    def test_existing_output_and_coordinate_overflow_rejected(self):
        output = self.root / 'preserve.odb'
        output.write_bytes(b'preserve')
        base = [sys.executable, str(Path(replicator.__file__)), str(self.root / 'source.odb')]
        run = subprocess.run(base + [str(output), '--nx', '2', '--ny', '2'],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(output.read_bytes(), b'preserve')
        overflow = self.root / 'overflow.odb'
        run = subprocess.run(base + [str(overflow), '--nx', '1000000', '--ny', '1'],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(overflow.exists())

    def test_masked_signal_wire_rejected(self):
        source = (self.root / 'source.def').read_text()
        source = source.replace('+ USE SIGNAL ;', '+ USE SIGNAL + ROUTED M1 ( 0 0 ) MASK 2 ( 1000 0 ) ;')
        (self.root / 'masked.def').write_text(source)
        output = self.root / 'masked.odb'
        run = subprocess.run([sys.executable, str(Path(replicator.__file__)), str(self.root / 'masked.def'),
                              str(output), '--nx', '2', '--ny', '1', '--lefs', str(self.root / 'source.lef')],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(output.exists())
        self.assertIn(b'mask coloring', run.stdout)


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Run the routed OpenDB integration fixtures against the separate C++ helper."""
import unittest
from unittest import mock

import replicate_odb_native as native
import test_replicate_odb as fixtures


class NativeReplicationTest(fixtures.ReplicateODBTest):
    # Native C++ supports mask types that the old Tcl bindings cannot pass.
    test_masked_signal_wire_rejected = None

    def test_masked_signal_wire_preserved(self):
        source = (self.root / 'source.def').read_text()
        source = source.replace('+ USE SIGNAL ;', '+ USE SIGNAL + ROUTED M1 ( 0 0 ) MASK 2 ( 1000 0 ) ;')
        (self.root / 'masked.def').write_text(source)
        self.replicate('masked.def', 'masked.odb', True)
        self.validate('masked.odb', True)


if __name__ == '__main__':
    # The inherited fixtures construct CLI commands from this module path.
    # Patch only the test process; the legacy implementation is unchanged.
    with mock.patch.object(fixtures.replicator, '__file__', native.__file__):
        unittest.main()

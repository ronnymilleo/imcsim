"""Tests for the pure logic of include_layers.py: graph building, layer violations and the baseline comparison."""

import unittest
from unittest import mock

import include_layers as layers

SOURCES = {
    "app-core/spice/spice_value.h": '#include "circuit/circuit.h"\n',
    "app-core/circuit/circuit.h": '#include "simulation/simulator.h"\n#include <vector>\n',
    "app-gui/schematic/wire.h": '#include "results/snapshot.h"\n',
    "app-gui/results/snapshot.h": '#include "schematic/wire.h"\n',
    "app-core/simulation/simulator.h": '#include "circuit/circuit.h"\n#include <nodalys/types.h>\n',
    "app-gui/application.h": '#include "windows/main_window.h"\n#include "simulation/simulator.h"\n',
    "app-gui/windows/main_window.h": '#include "ui_elements/ui_part.h"\n',
    "app-gui/ui_elements/ui_part.h": '#include "helpers.h"\n',
    "app-gui/helpers.h": "",
    "nodalys/include/nodalys/types.h": "",
}


class IncludeLayersTests(unittest.TestCase):
    def test_parses_quoted_and_angle_includes_with_their_lines(self):
        text = '// header\n#include "a/b.h"\n  #  include <c/d.h>\nint x;\n'
        self.assertEqual(layers.parse_includes(text), [(2, "a/b.h", False), (3, "c/d.h", True)])

    def test_files_at_the_library_root_form_the_root_node(self):
        includes = layers.build_includes(SOURCES)
        edges = layers.count_edges(includes)
        self.assertEqual(edges[("app-gui/root", "app-gui/windows")], 1)
        self.assertEqual(edges[("app-gui/ui_elements", "app-gui/root")], 1)

    def test_counts_includes_between_folders_and_skips_the_same_folder(self):
        edges = layers.count_edges(layers.build_includes(SOURCES))
        self.assertEqual(edges[("app-gui/schematic", "app-gui/results")], 1)
        self.assertEqual(edges[("app-gui/results", "app-gui/schematic")], 1)
        self.assertNotIn(("app-gui/schematic", "app-gui/schematic"), edges)

    def test_an_include_that_goes_up_a_layer_is_a_violation(self):
        violations = layers.find_violations(layers.build_includes(SOURCES))
        keys = {item.key for item in violations}
        self.assertIn("app-core/spice/spice_value.h -> circuit/circuit.h", keys)
        self.assertIn("app-gui/ui_elements/ui_part.h -> helpers.h", keys)

    def test_an_include_that_goes_up_between_two_gui_folders_is_a_violation(self):
        keys = {item.key for item in layers.find_violations(layers.build_includes(SOURCES))}
        self.assertIn("app-gui/schematic/wire.h -> results/snapshot.h", keys)
        self.assertNotIn("app-gui/results/snapshot.h -> schematic/wire.h", keys)

    def test_folders_in_the_same_layer_may_include_each_other(self):
        merged_layers = [[layers.ROOT_FOLDER], ["schematic", "results"]]
        with mock.patch.dict(layers.FOLDER_LAYERS, {layers.GUI: merged_layers + [["ui_elements", "windows"]]}):
            keys = {item.key for item in layers.find_violations(layers.build_includes(SOURCES))}
        self.assertNotIn("app-gui/schematic/wire.h -> results/snapshot.h", keys)
        self.assertNotIn("app-gui/results/snapshot.h -> schematic/wire.h", keys)

    def test_a_library_may_include_the_ones_below_it_but_not_above(self):
        sources = dict(SOURCES)
        sources["app-core/circuit/circuit.h"] += '#include "windows/main_window.h"\n'
        sources["nodalys/include/nodalys/types.h"] = '#include "circuit/circuit.h"\n'
        keys = {item.key for item in layers.find_violations(layers.build_includes(sources))}
        self.assertIn("app-core/circuit/circuit.h -> windows/main_window.h", keys)
        self.assertIn("nodalys/include/nodalys/types.h -> circuit/circuit.h", keys)
        self.assertNotIn("app-gui/application.h -> simulation/simulator.h", keys)
        self.assertNotIn("app-core/simulation/simulator.h -> nodalys/types.h", keys)

    def test_a_folder_without_a_layer_is_an_error(self):
        sources = {"app-core/newfolder/a.h": '#include "circuit/circuit.h"\n', "app-core/circuit/circuit.h": ""}
        with self.assertRaises(KeyError):
            layers.find_violations(layers.build_includes(sources))

    def test_baseline_comparison_reports_new_and_gone_violations(self):
        violations = layers.find_violations(layers.build_includes(SOURCES))
        known = {item.key for item in violations}
        self.assertEqual(layers.compare_with_baseline(violations, known), ([], []))

        new_violations, stale = layers.compare_with_baseline(violations, known - {violations[0].key} | {"gone.h -> x.h"})
        self.assertEqual([item.key for item in new_violations], [violations[0].key])
        self.assertEqual(stale, ["gone.h -> x.h"])

    def test_the_diagram_is_deterministic_and_marks_upward_edges(self):
        first = layers.render_diagram(layers.build_includes(SOURCES))
        second = layers.render_diagram(layers.build_includes(dict(reversed(list(SOURCES.items())))))
        self.assertEqual(first, second)
        self.assertIn("app_core_spice -.->|1| app_core_circuit", first)
        self.assertIn("app_core_simulation -->|1| app_core_circuit", first)


if __name__ == "__main__":
    unittest.main()

"""Tests for the pure parsing, relationship inference, cycle detection and audit checks in class_diagram.py."""

import unittest

import class_diagram as cd


class ClassDiagramTests(unittest.TestCase):
    def test_extracts_inheritance_brief_virtuals_and_private_order(self):
        src = cd.SourceFile(
            rel_path="nodalys/src/devices/sample.h",
            module="devices",
            text="""
            /**
             * @class   SampleDevice
             * @brief   Models a sample two-terminal element.
             */
            class SampleDevice : public Device {
            public:
                virtual void Stamp(MnaSystem &sys) = 0;
                void Reset();
            private:
                struct State { double Value = 0.0; };
                void HelperBeforeMembers();
                int m_NodeA = 0;
            };
            """,
            lines=14,
        )
        types = cd.extract_types_from_file(src)
        self.assertEqual(len(types), 1)
        node = types[0]
        self.assertEqual(node["id"], "SampleDevice")
        self.assertEqual(node["bases"], ["Device"])
        self.assertEqual(node["stereotype"], "«abstract class»")
        self.assertEqual(node["summary"], "Models a sample two-terminal element.")
        self.assertEqual(node["virtual_count"], 1)
        self.assertEqual(node["pure_virtual_count"], 1)
        self.assertTrue(node["private_order_inverted"])

    def test_infers_composition_aggregation_usage_and_sibling_nested_smell(self):
        files = [
            cd.SourceFile(
                rel_path="nodalys/src/devices/base.h",
                module="devices",
                text="""
                class VoltageSource {
                public:
                    using Waveform = int;
                };
                class CurrentSource {
                public:
                    void Apply(const TimeContext &ctx);
                private:
                    VoltageSource::Waveform m_Wave;
                };
                struct TimeContext { double Time = 0.0; };
                class Holder {
                private:
                    std::unique_ptr<VoltageSource> m_Owned;
                    CurrentSource *m_Ref = nullptr;
                };
                """,
                lines=16,
            )
        ]
        nodes = cd.extract_types_from_file(files[0])
        edges = {(e["from"], e["to"], e["type"]) for e in cd.infer_relationships(nodes, files)}
        self.assertIn(("Holder", "VoltageSource", "compose"), edges)
        self.assertIn(("Holder", "CurrentSource", "agg"), edges)
        self.assertIn(("CurrentSource", "TimeContext", "uses"), edges)
        self.assertIn(("CurrentSource", "VoltageSource", "smell"), edges)

    def test_detects_indirect_module_cycle_through_public_header(self):
        files = [
            cd.SourceFile("nodalys/include/nodalys/expression.h", "public-api", "", 1),
            cd.SourceFile("nodalys/src/expression/expr.cpp", "expression", '#include "parser/num.h"\n', 1),
            cd.SourceFile("nodalys/src/parser/ref.cpp", "parser", '#include "nodalys/expression.h"\n', 1),
        ]
        graph = cd.build_module_graph(files, cd.TARGETS["nodalys"])
        self.assertEqual(graph["cycles"], [["expression", "parser", "expression"]])

    def test_live_audit_flags_const_cast_dynamic_cast_and_missing_nodiscard(self):
        files = [
            cd.SourceFile(
                "nodalys/include/nodalys/api.h",
                "public-api",
                "std::expected<int, std::string> Run(const int x);\n"
                "inline int Helper(const int v) {\n"
                "    const int local = v + 1;\n"
                "    return local;\n"
                "}\n",
                5,
            ),
            cd.SourceFile(
                "nodalys/src/api/api.cpp",
                "api",
                "auto &c = const_cast<Circuit &>(in);\nauto *d = dynamic_cast<Resistor *>(p);\n(void)temp;\n",
                3,
            ),
        ]
        graph = cd.build_module_graph(files, cd.TARGETS["nodalys"])
        checks, kpis = cd.run_live_audit(files, [], graph)
        by_id = {c["id"]: c for c in checks}
        self.assertEqual(kpis["const_cast"], 1)
        self.assertEqual(kpis["dynamic_cast"], 1)
        self.assertEqual(kpis["missing_nodiscard"], 1)
        self.assertEqual(by_id["A1"]["status"], "open")
        self.assertEqual(by_id["A6"]["count"], 1)
        self.assertEqual(by_id["A9"]["count"], 1)
        self.assertEqual(by_id["A12"]["status"], "resolved")


if __name__ == "__main__":
    unittest.main()

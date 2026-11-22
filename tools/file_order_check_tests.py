"""Tests for canonical declaration ordering rules checked by file_order_check.py."""

import unittest

import file_order_check as checker


class FileOrderCheckTests(unittest.TestCase):
    def test_accepts_canonical_header_order(self):
        code = """
        namespace Nodalys {
        class Impl;
        inline constexpr int MAX_NODES = 10;
        using NodeId = int;
        enum class Mode { DC, AC };
        struct Config { Mode RunMode = Mode::DC; };
        class Solver {
        public:
            using Result = int;
            static constexpr int LIMIT = 4;
            Solver();
            int Run() const;
        private:
            struct State { double Value = 0.0; };
            static constexpr double TOL = 1e-6;
            int m_Count = 0;
            void Step();
        };
        using AnyConfig = Config;
        int Solve(const Config &cfg);
        } // namespace Nodalys
        """
        self.assertEqual(checker.check_file("nodalys/include/nodalys/sample.h", code), [])

    def test_flags_enum_after_struct_and_constant_after_struct(self):
        code = """
        namespace Nodalys {
        struct A { int X = 0; };
        enum class Kind { ONE };
        inline constexpr double SCALE = 1.0;
        } // namespace Nodalys
        """
        violations = checker.check_file("nodalys/include/nodalys/bad.h", code)
        self.assertEqual(len(violations), 2)
        self.assertIn("enum 'Kind' appears after struct", violations[0].message)
        self.assertIn("constant 'SCALE' appears after struct", violations[1].message)

    def test_allows_dependent_constant_or_alias_after_type_it_uses(self):
        code = """
        namespace Nodalys {
        struct Settings { double Value = 1.0; };
        constexpr Settings DEFAULT_SETTINGS{.Value = 2.0};
        using SettingsList = Settings;
        } // namespace Nodalys
        """
        self.assertEqual(checker.check_file("nodalys/src/sample.h", code), [])

    def test_flags_private_methods_before_members_and_wrong_access_order(self):
        code = """
        namespace Nodalys {
        class Broken {
        private:
            void Helper();
            int m_Value = 0;
        public:
            int Get() const;
        };
        } // namespace Nodalys
        """
        violations = checker.check_file("nodalys/src/broken.h", code)
        messages = [v.message for v in violations]
        self.assertTrue(any("data member appears after method" in m for m in messages))
        self.assertTrue(any("'public:' appears after" in m for m in messages))

    def test_checks_anonymous_namespace_order_in_cpp(self):
        code = """
        namespace Nodalys {
        namespace {
        struct HelperState { int X = 0; };
        constexpr double EPS = 1e-9;
        void Helper() {}
        } // namespace
        void PublicFn() {}
        } // namespace Nodalys
        """
        violations = checker.check_file("nodalys/src/sample.cpp", code)
        self.assertEqual(len(violations), 1)
        self.assertIn("constant 'EPS' appears after struct", violations[0].message)

    def test_handles_default_braced_args_std_function_and_alias_chains(self):
        code = """
        namespace Nodalys {
        using NodeId = int;
        inline constexpr NodeId GROUND_NODE = 0;
        using Handler = int (*)(NodeId);
        enum class Mode { A, B };
        struct Params { int X = 0; };
        class Device {
        public:
            void Configure(const Params &params = {});
            void SetCallback(std::function<void(int)> cb);
        private:
            std::function<void(int)> m_Callback;
            void Notify();
        };
        } // namespace Nodalys
        """
        self.assertEqual(checker.check_file("nodalys/src/sample.h", code), [])


if __name__ == "__main__":
    unittest.main()

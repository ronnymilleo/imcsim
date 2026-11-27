"""Tests for C++ Doxygen style rules checked by doxygen_check.py."""

import pathlib
import unittest

import doxygen_check as checker

VALID_HEADER = """\
/**
 * @file    sample.h
 * @brief   Reads and formats sample values.
 */

#ifndef IMCSIM_SAMPLE_H
#define IMCSIM_SAMPLE_H

namespace Core {

/**
 * @struct  SampleConfig
 * @brief   Settings of a sample run.
 */
struct SampleConfig {
    double Scale = 1.0; ///< Multiplier applied to every reading.
    /// Long explanation of why the offset defaults to zero and exceeds the 120-column single-line trailing limit when combined.
    double Offset = 0.0;
};

// Parsing
int ParseSample(double value);

} // namespace Core

#endif // IMCSIM_SAMPLE_H
"""

VALID_SOURCE = """\
/**
 * @file    sample.cpp
 * @brief   Reads and formats sample values.
 */

#include "sample.h"

namespace Core {

namespace {

/**
 * @brief   Clamps a reading to the non-negative range with \\f$x \\ge 0\\f$.
 */
double ClampPositive(const double value) {
    return value > 0.0 ? value : 0.0;
}

} // namespace

/**
 * @brief   Rounds a scaled sample to an integer count.
 * @param[in] value  Raw reading in volts.
 * @return  Rounded sample count.
 */
int ParseSample(const double value) {
    // Plain comment inside function body is fine
    return static_cast<int>(ClampPositive(value));
}

} // namespace Core
"""


class DoxygenCheckTests(unittest.TestCase):
    def test_accepts_compliant_header_and_source_pair(self):
        self.assertEqual(checker.check_file_text("app-core/sample.h", VALID_HEADER), [])
        self.assertEqual(checker.check_file_text("app-core/sample.cpp", VALID_SOURCE), [])
        self.assertIsNone(
            checker.check_pair_brief("app-core/sample.h", VALID_HEADER, "app-core/sample.cpp", VALID_SOURCE)
        )

    def test_flags_missing_or_mismatched_file_header_and_pair_brief(self):
        wrong_name = VALID_HEADER.replace("@file    sample.h", "@file    other.h")
        issues = checker.check_file_text("app-core/sample.h", wrong_name)
        self.assertEqual(len(issues), 1)
        self.assertIn("instead of 'sample.h'", issues[0].message)

        diff_brief = VALID_SOURCE.replace("Reads and formats sample values.", "Implementation of sample.")
        pair_issue = checker.check_pair_brief("app-core/sample.h", VALID_HEADER, "app-core/sample.cpp", diff_brief)
        self.assertIsNotNone(pair_issue)
        self.assertIn("@brief does not match", pair_issue.message)

    def test_flags_disallowed_tags_bare_param_and_unescaped_dollar_math(self):
        bad = """\
/**
 * @file    bad.cpp
 * @brief   Bad tags and math $x + y$ beside `$HOME`.
 * @author  Someone
 */

/**
 * @brief   Computes something.
 * @param   value  Missing direction.
 * @see     Other
 */
int Compute(const int value) {
    return value;
}
"""
        messages = [v.message for v in checker.check_file_text("app-core/bad.cpp", bad)]
        self.assertTrue(any("disallowed Doxygen tag '@author'" in m for m in messages))
        self.assertTrue(any("disallowed Doxygen tag '@see'" in m for m in messages))
        self.assertTrue(any("@param missing direction" in m for m in messages))
        self.assertTrue(any("unescaped '$'" in m for m in messages))

    def test_flags_line_comments_above_functions_including_wrapped_return_types(self):
        code = """\
/**
 * @file    helpers.cpp
 * @brief   Helper functions.
 */

namespace Core {

/// Triple-slash above function is forbidden.
int First(const int x) {
    return x;
}

// Plain comment above wrapped return-type definition is also forbidden
std::pair<int, int>
Second(const int x) {
    return {x, x};
}

} // namespace Core
"""
        messages = [v.message for v in checker.check_file_text("app-core/helpers.cpp", code)]
        self.assertEqual(len(messages), 2)
        self.assertTrue(any("/// line comment above function" in m for m in messages))
        self.assertTrue(any("// comment right above function definition" in m for m in messages))

    def test_flags_noninline_header_doxygen_in_internal_header_but_allows_public_or_inline(self):
        internal_bad = VALID_HEADER.replace(
            "// Parsing\nint ParseSample(double value);",
            "/**\n * @brief   Misplaced block.\n * @param[in] value  Input.\n * @return  Count.\n */\nint ParseSample(double value);",
        )
        self.assertEqual(len(checker.check_file_text("app-core/sample.h", internal_bad)), 1)
        self.assertEqual(checker.check_file_text("nodalys/include/nodalys/sample.h", internal_bad), [])

    def test_flags_field_comment_fitting_as_trailing_and_overlong_trailing(self):
        short_above = """\
/**
 * @file    fields.h
 * @brief   Struct fields.
 */

struct Box {
    /// Fits on the same line.
    double Width = 1.0;
};
"""
        issues = checker.check_file_text("app-gui/fields.h", short_above)
        self.assertEqual(len(issues), 1)
        self.assertIn("fits as ///<", issues[0].message)

        overlong = (
            "/**\n * @file    fields.h\n * @brief   Struct fields.\n */\nstruct Box {\n"
            + f"    double Width = 1.0; ///< {'x' * 105}\n"
            + "};\n"
        )
        issues_long = checker.check_file_text("app-gui/fields.h", overlong)
        self.assertEqual(len(issues_long), 1)
        self.assertIn("exceeds 120 columns", issues_long[0].message)

    def test_flags_undocumented_test_helper_struct_and_filters_doxygen_stderr(self):
        test_code = """\
/**
 * @file    sample_tests.cpp
 * @brief   Tests for sample.
 */

namespace {
struct Fixture {
    int Value = 0;
};
} // namespace
"""
        issues = checker.check_file_text("tests/sample_tests.cpp", test_code, is_tests=True)
        self.assertEqual(len(issues), 1)
        self.assertIn("undocumented test helper type", issues[0].message)

        root = pathlib.Path("/repo")
        stderr = "\n".join(
            (
                "warning: No output formats selected! Set at least one of the main GENERATE_* options to YES.",
                "/repo/app-core/foo.h:12: warning: Member Gain (variable) of struct Core::Amp is not documented.",
                "/repo/app-core/foo.cpp:20: warning: parameters of member Core::Run are not documented",
            )
        )
        filtered = checker.filter_doxygen_stderr(stderr, root, is_tests=False)
        self.assertEqual(filtered, ["app-core/foo.cpp:20: warning: parameters of member Core::Run are not documented"])

    def test_handles_digit_separators_and_raw_strings_inside_test_bodies(self):
        test_code = '''\
/**
 * @file    sep_tests.cpp
 * @brief   Tests with digit separators and raw strings.
 */

TEST_CASE("Digit separators do not break brace tracking", "[sep]") {
    const int samples = 20'000;
    const char *raw = R"(unbalanced { brace)";
    // Comment inside TEST_CASE body stays allowed
    CheckResult(samples, raw);
}
'''
        self.assertEqual(checker.check_file_text("tests/sep_tests.cpp", test_code, is_tests=True), [])


if __name__ == "__main__":
    unittest.main()

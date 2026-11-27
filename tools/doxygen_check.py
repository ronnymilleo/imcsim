#!/usr/bin/env python3
"""Checks Doxygen 1.18 warnings and C++ documentation style rules across the codebase.

Enforces both Doxygen's own checks (WARN_IF_UNDOCUMENTED, WARN_NO_PARAMDOC, WARN_IF_DOC_ERROR)
and the project's C++ documentation guidelines that Doxygen does not check:
  - Every .h and .cpp starts with a @file / @brief block naming its own file, and paired .h / .cpp share @brief
  - Only allowed Doxygen tags (@no @pre, @warning, @see, @throws, @example, @defgroup, @name, @author, @date, @version)
  - Every @param carries a direction ([in], [out], or [in,out])
  - Formulas in Doxygen comments use \\f$...\\f$ or \\f[...\\f], never bare $...$
  - Functions never carry /// or // line comments above their definitions
  - Non-inline declarations in internal headers do not carry Doxygen blocks (definitions in .cpp do)
  - Single-line /// comments on fields and enum values use trailing ///< when fitting in <= 120 columns

Usage:
    python tools/doxygen_check.py [nodalys|app-core|app-gui|tests|main ...]
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent

TARGETS: dict[str, list[str]] = {
    "nodalys": ["nodalys/include", "nodalys/src"],
    "app-core": ["app-core"],
    "app-gui": ["app-gui"],
    "tests": ["tests", "nodalys/tests"],
    "main": ["main.cpp"],
}

BAD_TAG_RE = re.compile(r"@(?:pre|warning|see|throws|example|defgroup|name|author|date|version)\b")
BARE_PARAM_RE = re.compile(r"@param(?!\s*\[(?:in|out|in,out)\])\b")
FILE_TAG_RE = re.compile(r"@file\s+(\S+)")
BRIEF_TAG_RE = re.compile(r"@brief\s+(.*?)(?=\n\s*\*\s*@\w+|\n\s*\*/)", re.DOTALL)
FIRST_INCLUDE_RE = re.compile(r'^#include\s+"([^"]+)"', re.MULTILINE)
NON_FIELD_DECL_RE = re.compile(
    r"^(?:struct|class|enum|using|typedef|namespace|template|public:|private:|protected:|#|inline\b|static\s+constexpr\b|constexpr\b)"
)
ENUM_VAL_DECL_RE = re.compile(r"^[A-Z][A-Z0-9_]*(?:\s*=\s*[^,;]+)?\s*,?$")


@dataclass(frozen=True)
class Violation:
    file: str
    line: int
    message: str

    def format(self) -> str:
        return f"{self.file}:{self.line}: {self.message}"


def strip_angles(text: str) -> str:
    """Removes balanced `<...>` template argument lists except after `operator`."""
    out: list[str] = []
    depth = 0
    for idx, ch in enumerate(text):
        if ch == "<" and not text[:idx].rstrip().endswith("operator"):
            depth += 1
        elif ch == ">" and depth > 0:
            depth -= 1
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def _try_advance_raw_string(text: str, pos: int) -> int | None:
    if pos > 0 and (text[pos - 1].isalnum() or text[pos - 1] == "_"):
        return None
    paren = text.find("(", pos + 2)
    if paren == -1:
        return None
    end_marker = f"){text[pos + 2 : paren]}\""
    end = text.find(end_marker, paren + 1)
    return None if end == -1 else end + len(end_marker)


def _advance_comment_or_quote(text: str, pos: int) -> int | None:
    n = len(text)
    ch = text[pos]
    nxt = text[pos + 1] if pos + 1 < n else ""
    if ch == "/" and nxt == "/":
        nl = text.find("\n", pos)
        return n if nl == -1 else nl + 1
    if ch == "/" and nxt == "*":
        end = text.find("*/", pos + 2)
        return n if end == -1 else end + 2
    if ch == "R" and nxt == '"':
        return _try_advance_raw_string(text, pos)
    if ch == "'" and pos > 0 and text[pos - 1].isalnum():
        return None
    if ch in ('"', "'"):
        i = pos + 1
        while i < n:
            if text[i] == "\\":
                i += 2
                continue
            if text[i] == ch or text[i] == "\n":
                return i + 1
            i += 1
        return n
    return None


def find_matching_brace(text: str, start: int) -> int:
    """Finds the matching `}` for `{` at `start`, skipping comments and string literals."""
    depth = 0
    i = start
    n = len(text)
    while i < n:
        jumped = _advance_comment_or_quote(text, i)
        if jumped is not None:
            i = jumped
            continue
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n - 1


def _classify_open_brace(text: str, pos: int, seen_close_paren: bool) -> str:
    prefix = text[max(0, pos - 250) : pos]
    clean = re.sub(r"//[^\n]*", "", prefix)
    clean = re.sub(r"/\*.*?\*/", "", clean, flags=re.DOTALL).strip()
    if re.search(r"\b(?:struct|class|enum|union)\b[^{;()]*$", clean) and not seen_close_paren:
        return "type"
    is_ns = bool(re.search(r"\bnamespace\b[^{;()]*$", clean))
    is_var_init = bool(re.search(r"=\s*(?:[A-Za-z0-9_:<>]+)?\s*$", clean))
    if seen_close_paren and not is_ns and not is_var_init:
        return "func"
    return "other"


def locate_scopes(text: str) -> tuple[set[int], set[int]]:
    """Returns `(function_body_lines, class_body_lines)` as 0-based line indices."""
    func_lines: set[int] = set()
    class_lines: set[int] = set()
    paren_depth = 0
    seen_close_paren = False
    i = 0
    n = len(text)
    while i < n:
        jumped = _advance_comment_or_quote(text, i)
        if jumped is not None:
            i = jumped
            continue
        ch = text[i]
        if ch == "(":
            paren_depth += 1
        elif ch == ")":
            paren_depth = max(0, paren_depth - 1)
            seen_close_paren = seen_close_paren or paren_depth == 0
        elif ch in (";", "}"):
            seen_close_paren = False
        elif ch == "{":
            kind = _classify_open_brace(text, i, seen_close_paren)
            seen_close_paren = False
            if kind in ("type", "func"):
                end_brace = find_matching_brace(text, i)
                start_ln = text[:i].count("\n")
                end_ln = text[:end_brace].count("\n")
                if kind == "type":
                    class_lines.update(range(start_ln + 1, end_ln))
                else:
                    func_lines.update(range(start_ln + 1, end_ln + 1))
                    i = end_brace + 1
                    continue
        i += 1
    return func_lines, class_lines


def extract_file_header_brief(text: str) -> tuple[str | None, str | None]:
    """Returns `(file_tag_name, normalized_brief)` from the leading `/** ... */` block."""
    stripped = text.lstrip()
    if not stripped.startswith("/**"):
        return None, None
    end = stripped.find("*/")
    if end == -1:
        return None, None
    block = stripped[: end + 2]
    file_match = FILE_TAG_RE.search(block)
    brief_match = BRIEF_TAG_RE.search(block)
    file_name = file_match.group(1) if file_match else None
    brief = (
        " ".join(ln.lstrip(" *").strip() for ln in brief_match.group(1).splitlines()).strip()
        if brief_match
        else None
    )
    return file_name, brief


def _check_header_and_tags(rel_path: str, text: str) -> list[Violation]:
    issues: list[Violation] = []
    expected_name = pathlib.Path(rel_path).name
    file_name, brief = extract_file_header_brief(text)
    if file_name is None or brief is None:
        issues.append(Violation(rel_path, 1, "missing leading @file / @brief Doxygen block"))
    elif file_name != expected_name:
        issues.append(Violation(rel_path, 1, f"@file names '{file_name}' instead of '{expected_name}'"))

    for match in BAD_TAG_RE.finditer(text):
        ln = text[: match.start()].count("\n") + 1
        issues.append(Violation(rel_path, ln, f"disallowed Doxygen tag '{match.group(0)}'"))

    for match in BARE_PARAM_RE.finditer(text):
        ln = text[: match.start()].count("\n") + 1
        issues.append(Violation(rel_path, ln, "@param missing direction ([in], [out], or [in,out])"))

    for match in re.finditer(r"/\*\*.*?\*/|///[^\n]*", text, flags=re.DOTALL):
        cleaned = re.sub(r"\\f\$.*?\\f\$|\\f\[.*?\\f\]|`[^`]*`", "", match.group(0), flags=re.DOTALL)
        if "$" in cleaned:
            ln = text[: match.start()].count("\n") + 1
            issues.append(Violation(rel_path, ln, "unescaped '$' in Doxygen comment (use \\f$...\\f$ or backticks)"))
    return issues


def _check_trailing_field_comment(rel_path: str, lines: list[str], idx: int) -> Violation | None:
    raw_line = lines[idx]
    s = raw_line.strip()
    ln = idx + 1
    if "///<" in raw_line:
        before = raw_line.split("///<", 1)[0].strip()
        if not before or (" " not in before and before.endswith(";")):
            return Violation(rel_path, ln, "wrapped ///< without declaration on the same line")
        if len(raw_line) > 120:
            return Violation(rel_path, ln, f"line with ///< exceeds 120 columns ({len(raw_line)})")
        return None

    is_single_slash3 = (
        idx + 1 < len(lines)
        and s.startswith("///")
        and not (idx > 0 and lines[idx - 1].strip().startswith("///"))
        and not lines[idx + 1].strip().startswith("///")
    )
    if not is_single_slash3:
        return None
    decl = lines[idx + 1]
    decl_s = decl.strip()
    indent = len(decl) - len(decl.lstrip())
    decl_no_angles = strip_angles(decl_s)
    is_field = (
        indent >= 4
        and not NON_FIELD_DECL_RE.match(decl_s)
        and decl_s.endswith(";")
        and "(" not in decl_no_angles.split("=", 1)[0]
    )
    is_enum_val = indent >= 4 and ENUM_VAL_DECL_RE.match(decl_s) is not None
    if is_field or is_enum_val:
        combined = f"{decl} ///< {s[3:].strip()}"
        if len(combined) <= 120:
            return Violation(rel_path, ln, f"single-line /// on field/enum fits as ///< ({len(combined)} cols)")
    return None


def _signature_kind(lines: list[str], idx: int) -> tuple[bool, bool, str]:
    """Returns `(is_func_decl_or_def, is_definition, sig_no_angles)` for statement starting at `idx`."""
    s = lines[idx].strip()
    if not s or s.startswith(("//", "/*", "*", "#", "TEST_CASE(", "using ", "typedef ", "struct ", "class ", "enum ", "namespace ")):
        return False, False, ""
    chunk = " ".join(ln.strip() for ln in lines[idx : min(len(lines), idx + 8)])
    sig = strip_angles(chunk)
    par = sig.find("(")
    if par == -1:
        return False, False, sig
    semi = sig.find(";")
    brace = sig.find("{")
    eq = sig.find("=")
    if eq != -1 and eq < par:
        return False, False, sig
    if semi != -1 and semi < par:
        return False, False, sig
    if brace != -1 and brace < par:
        return False, False, sig
    is_func = bool(
        re.match(
            r"^(?:template\s*\s*)?(?:\[\[[A-Za-z0-9_:]+\]\]\s*)*(?:inline\s+|static\s+|virtual\s+|explicit\s+|constexpr\s+)*(?:[A-Za-z_~][A-Za-z0-9_:&\*\s]*?)\s*\(",
            sig,
        )
    )
    is_def = brace != -1 and (semi == -1 or brace < semi)
    return is_func, is_def, sig


def _check_function_comments_and_placement(
    rel_path: str,
    text: str,
    lines: list[str],
    idx: int,
    in_class: bool,
    is_tests: bool,
) -> Violation | None:
    if idx == 0:
        return None
    is_func, is_def, sig = _signature_kind(lines, idx)
    if not is_func:
        return None
    prev = lines[idx - 1].strip()
    ln = idx + 1
    if prev.startswith("///") and not prev.startswith("///<"):
        return Violation(rel_path, ln, f"/// line comment above function (use /** */ block or remove): {lines[idx].strip()[:60]}")
    if prev.startswith("//") and not prev.startswith("///"):
        words = prev[2:].strip().split()
        is_topic = (in_class or rel_path.endswith(".h")) and len(words) <= 5 and not prev.endswith(".")
        if is_def and not is_topic:
            return Violation(rel_path, ln, f"// comment right above function definition: {lines[idx].strip()[:60]}")

    is_internal_header = rel_path.endswith(".h") and not is_tests and not rel_path.startswith("nodalys/include/")
    if is_internal_header and prev == "*/" and not is_def:
        is_pure_virtual = bool(re.search(r"=\s*0\s*;", sig))
        is_default_or_delete = bool(re.search(r"=\s*(?:default|delete)\s*;", sig))
        is_template = lines[idx].strip().startswith("template") or (in_class and "template <" in text[: text.find(lines[idx])])
        if not is_pure_virtual and not is_default_or_delete and not is_template:
            return Violation(rel_path, ln, f"Doxygen block on non-inline header declaration (move to .cpp): {lines[idx].strip()[:60]}")
    return None


def _check_test_helper_type(rel_path: str, lines: list[str], idx: int) -> Violation | None:
    s = lines[idx].strip()
    if not re.match(r"^(?:struct|class|enum)\s+[A-Za-z_]", s) or s.endswith(";"):
        return None
    prev = lines[idx - 1].strip() if idx > 0 else ""
    if prev.startswith("template"):
        prev = lines[idx - 2].strip() if idx > 1 else ""
    if prev != "*/" and not prev.startswith("///"):
        return Violation(rel_path, idx + 1, f"undocumented test helper type: {s[:60]}")
    return None


def check_file_text(rel_path: str, text: str, is_tests: bool = False) -> list[Violation]:
    """Checks all non-Doxygen custom documentation rules on a single file's contents."""
    issues = _check_header_and_tags(rel_path, text)
    lines = text.splitlines()
    func_lines, class_lines = locate_scopes(text)
    for idx in range(len(lines)):
        if idx in func_lines:
            continue
        trailing = _check_trailing_field_comment(rel_path, lines, idx)
        if trailing is not None:
            issues.append(trailing)
        fn_issue = _check_function_comments_and_placement(rel_path, text, lines, idx, idx in class_lines, is_tests)
        if fn_issue is not None:
            issues.append(fn_issue)
        if is_tests:
            type_issue = _check_test_helper_type(rel_path, lines, idx)
            if type_issue is not None:
                issues.append(type_issue)
    return issues


def check_pair_brief(header_rel: str, header_text: str, source_rel: str, source_text: str) -> Violation | None:
    """Checks that a `.h` and its `.cpp` share the same `@brief` summary."""
    _, h_brief = extract_file_header_brief(header_text)
    _, c_brief = extract_file_header_brief(source_text)
    if h_brief and c_brief and h_brief != c_brief:
        return Violation(source_rel, 1, f"@brief does not match {header_rel} ('{c_brief}' vs '{h_brief}')")
    return None


def _resolve_paired_header(cpp_path: pathlib.Path, source_text: str, root: pathlib.Path) -> pathlib.Path | None:
    match = FIRST_INCLUDE_RE.search(source_text)
    if not match:
        return None
    inc = pathlib.Path(match.group(1))
    if inc.stem != cpp_path.stem:
        return None
    candidates = (
        cpp_path.parent / inc,
        root / "app-core" / inc,
        root / "app-gui" / inc,
        root / "nodalys/include" / inc,
        root / "nodalys/src" / inc,
    )
    return next((cand for cand in candidates if cand.is_file()), None)


def filter_doxygen_stderr(stderr: str, root: pathlib.Path, is_tests: bool = False) -> list[str]:
    """Filters Doxygen stderr down to actionable warnings for the given target kind."""
    prefix = str(root) + "/"
    issues: list[str] = []
    for raw in stderr.splitlines():
        line = raw.strip()
        if not line or line.startswith("warning: No output formats selected"):
            continue
        if "Member " in line and "(variable)" in line and "is not documented" in line:
            continue
        if is_tests:
            if "TEST_CASE(" in line or "Incomplete input: scope for class Catch::StringMaker" in line:
                continue
            if "is not documented" in line and "Catch::StringMaker" not in line:
                continue
            if "parameters of member " in line or "return type of member " in line:
                continue
        issues.append(line.replace(prefix, ""))
    return issues


def run_doxygen(paths: list[str], root: pathlib.Path = PROJECT_ROOT, is_tests: bool = False) -> list[str]:
    """Runs `doxygen -` on `paths` and returns actionable warning lines."""
    cfg = "\n".join(
        (
            "PROJECT_NAME = imcsim",
            f"INPUT = {' '.join(paths)}",
            "RECURSIVE = YES",
            "FILE_PATTERNS = *.h *.cpp",
            "EXTRACT_ALL = NO",
            "EXTRACT_PRIVATE = NO",
            "EXTRACT_STATIC = NO",
            f"EXTRACT_ANON_NSPACES = {'YES' if is_tests else 'NO'}",
            "HIDE_UNDOC_MEMBERS = NO",
            "WARN_IF_UNDOCUMENTED = YES",
            "WARN_IF_DOC_ERROR = YES",
            "WARN_NO_PARAMDOC = YES",
            "GENERATE_HTML = NO",
            "GENERATE_LATEX = NO",
            "QUIET = YES",
        )
    )
    proc = subprocess.run(["doxygen", "-"], cwd=root, input=cfg, text=True, capture_output=True, check=False)
    return filter_doxygen_stderr(proc.stderr, root, is_tests=is_tests)


def iter_target_files(paths: list[str], root: pathlib.Path = PROJECT_ROOT) -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    for rel in paths:
        target = root / rel
        if target.is_file():
            out.append(target)
        elif target.is_dir():
            out.extend(sorted(p for p in target.rglob("*") if p.suffix in (".h", ".cpp")))
    return out


def check_target(target_name: str, root: pathlib.Path = PROJECT_ROOT) -> tuple[list[str], list[Violation]]:
    paths = TARGETS[target_name]
    is_tests = target_name == "tests"
    doxy_warnings = run_doxygen(paths, root=root, is_tests=is_tests)
    violations: list[Violation] = []
    for file_path in iter_target_files(paths, root=root):
        rel = file_path.relative_to(root).as_posix()
        text = file_path.read_text(encoding="utf-8")
        violations.extend(check_file_text(rel, text, is_tests=is_tests))
        if file_path.suffix == ".cpp":
            header_path = _resolve_paired_header(file_path, text, root)
            if header_path is not None:
                pair_issue = check_pair_brief(
                    header_path.relative_to(root).as_posix(),
                    header_path.read_text(encoding="utf-8"),
                    rel,
                    text,
                )
                if pair_issue is not None:
                    violations.append(pair_issue)
    return doxy_warnings, violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check Doxygen 1.18 warnings and C++ documentation style rules.")
    parser.add_argument("targets", nargs="*", choices=[*TARGETS.keys()], help="Targets to check (default: all).")
    args = parser.parse_args(argv)
    selected = args.targets or list(TARGETS.keys())

    total_issues = 0
    for name in selected:
        doxy_warnings, violations = check_target(name)
        count = len(doxy_warnings) + len(violations)
        total_issues += count
        if count == 0:
            print(f"OK: 0 Doxygen or documentation rule issues in '{name}'.")
            continue
        print(f"FAIL: {len(doxy_warnings)} Doxygen warning(s) and {len(violations)} style issue(s) in '{name}':")
        for warning in doxy_warnings:
            print(f"  [doxygen] {warning}")
        for violation in violations:
            print(f"  [style]   {violation.format()}")
    return 1 if total_issues > 0 else 0


if __name__ == "__main__":
    sys.exit(main())

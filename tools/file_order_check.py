#!/usr/bin/env python3
"""Checks canonical declaration ordering in C++ headers, classes and anonymous namespaces.

Canonical order inside a header namespace or an anonymous namespace in a .cpp:
  0. Forward declarations
  1. Constants (constexpr / inline constexpr)
  2. Type aliases (using)
  3. Enums (enum class)
  4. Structs (struct)
  5. Classes (class)
  6. Composite aliases or constants that reference a type/alias declared earlier in the same file
  7. Free functions

Canonical order inside a class:
  Access sections: public -> protected -> private
  Inside public:   nested types -> static constexpr -> constructors/methods
  Inside protected/private: friend -> nested types -> static constexpr -> data members -> methods

Usage:
    .venv/bin/python tools/file_order_check.py nodalys
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = (".h", ".cpp")
EXCLUDED_SEGMENTS = ("/tests/", "/vendor/", "/build/", "/cmake-build-")

TARGETS: dict[str, list[str]] = {
    "nodalys": [
        "nodalys/include",
        "nodalys/src",
    ],
}

NAMESPACE_KIND_NAMES = {
    0: "forward declaration",
    1: "constant",
    2: "type alias",
    3: "enum",
    4: "struct",
    5: "class",
    6: "dependent alias/constant",
    7: "free function",
}

CLASS_ITEM_NAMES = {
    0: "friend",
    1: "nested type",
    2: "constant",
    3: "data member",
    4: "method",
}

ACCESS_RANK = {"public": 0, "protected": 1, "private": 2}


@dataclass(frozen=True)
class Violation:
    file: str
    line: int
    scope: str
    message: str

    def format(self) -> str:
        return f"{self.file}:{self.line}: [{self.scope}] {self.message}"


@dataclass
class _ClassScanState:
    access: str
    max_access_rank: int = -1
    max_item_rank: int = -1
    max_item_name: str = ""


def _skip_line_comment(code: str, start: int, out: list[str]) -> int:
    i = start
    n = len(code)
    while i < n and code[i] != "\n":
        out.append(" ")
        i += 1
    return i


def _skip_block_comment(code: str, start: int, out: list[str]) -> int:
    n = len(code)
    out.extend((" ", " "))
    i = start + 2
    while i < n:
        if code[i] == "\n":
            out.append("\n")
            i += 1
        elif code[i] == "*" and i + 1 < n and code[i + 1] == "/":
            out.extend((" ", " "))
            return i + 2
        else:
            out.append(" ")
            i += 1
    return i


def _try_skip_raw_string(code: str, start: int, out: list[str]) -> int | None:
    if start > 0 and (code[start - 1].isalnum() or code[start - 1] == "_"):
        return None
    paren = code.find("(", start + 2)
    if paren == -1:
        return None
    delim = code[start + 2 : paren]
    end_marker = f"){delim}\""
    end_idx = code.find(end_marker, paren + 1)
    if end_idx == -1:
        return None
    span = code[start : end_idx + len(end_marker)]
    out.append("".join("\n" if c == "\n" else " " for c in span))
    return end_idx + len(end_marker)


def _skip_quoted_literal(code: str, start: int, out: list[str]) -> int:
    quote = code[start]
    n = len(code)
    out.append(" ")
    i = start + 1
    while i < n:
        c = code[i]
        if c == "\\":
            out.append(" ")
            if i + 1 < n:
                out.append("\n" if code[i + 1] == "\n" else " ")
            i += 2
            continue
        if c == "\n":
            out.append("\n")
            return i + 1
        out.append(" ")
        i += 1
        if c == quote:
            return i
    return i


def strip_comments_and_strings(code: str) -> str:
    """Replaces comments and string/char literals with spaces while keeping newlines intact."""
    out: list[str] = []
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        nxt = code[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            i = _skip_line_comment(code, i, out)
        elif ch == "/" and nxt == "*":
            i = _skip_block_comment(code, i, out)
        elif ch == "R" and nxt == '"':
            advanced = _try_skip_raw_string(code, i, out)
            if advanced is not None:
                i = advanced
            else:
                out.append(ch)
                i += 1
        elif ch in ('"', "'"):
            i = _skip_quoted_literal(code, i, out)
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def find_matching_brace(text: str, open_pos: int) -> int:
    """Finds the matching '}' for '{' at open_pos."""
    depth = 0
    for idx in range(open_pos, len(text)):
        c = text[idx]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return idx
    return -1


def strip_template_args(text: str) -> str:
    """Strips balanced template argument lists `<...>` except after `operator`."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == "<" and not re.search(r"\boperator\s*$", "".join(out).rstrip()):
            depth = 1
            j = i + 1
            while j < n and depth > 0:
                depth += 1 if text[j] == "<" else (-1 if text[j] == ">" else 0)
                j += 1
            if depth == 0:
                i = j
                continue
        out.append(text[i])
        i += 1
    return "".join(out)


def _advance_past_optional_semicolon(text: str, brace_idx: int) -> int:
    j = brace_idx + 1
    n = len(text)
    while j < n and text[j] in " \t\r\n":
        j += 1
    return j + 1 if (j < n and text[j] == ";") else brace_idx + 1


def _extract_decl_tuple(block: str, start: int, end_idx: int, base_line: int, has_brace: bool) -> tuple[int, str, str] | None:
    raw = block[start:end_idx]
    stripped = raw.lstrip()
    if not stripped:
        return None
    lead_ws = len(raw) - len(stripped)
    line_no = base_line + block[: start + lead_ws].count("\n")
    clean_lines = [ln for ln in stripped.splitlines() if not ln.lstrip().startswith("#")]
    full = "\n".join(clean_lines).strip()
    if not full:
        return None
    header = full.split("{", 1)[0].strip() if has_brace else full.rstrip(";").strip()
    return (line_no, re.sub(r"\s+", " ", header), full)


def _update_nesting(ch: str, depth: int, paren_depth: int) -> tuple[int, int]:
    if ch == "(":
        return depth, paren_depth + 1
    if ch == ")":
        return depth, max(0, paren_depth - 1)
    if ch == "{":
        return depth + 1, paren_depth
    if ch == "}":
        return depth - 1, paren_depth
    return depth, paren_depth


def _skip_preprocessor_line(block: str, start: int, i: int) -> tuple[int, int]:
    nl = block.find("\n", i)
    nxt = len(block) if nl == -1 else nl + 1
    chunk = block[start:nxt]
    new_start = nxt if (not chunk.strip() or chunk.lstrip().startswith("#")) else start
    return new_start, nxt


def split_top_level_decls(block: str, base_line: int) -> list[tuple[int, str, str]]:
    """Splits a namespace block into top-level declarations as (start_line, header_text, full_text)."""
    decls: list[tuple[int, str, str]] = []
    n = len(block)
    i = depth = paren_depth = start = 0

    while i < n:
        c = block[i]
        if depth == 0 and paren_depth == 0 and c == "#":
            start, i = _skip_preprocessor_line(block, start, i)
            continue
        depth, paren_depth = _update_nesting(c, depth, paren_depth)
        if depth == 0 and paren_depth == 0 and c in ("}", ";"):
            end_idx = _advance_past_optional_semicolon(block, i) if c == "}" else i + 1
            if item := _extract_decl_tuple(block, start, end_idx, base_line, c == "}"):
                decls.append(item)
            start = i = end_idx
            continue
        i += 1

    return decls


def extract_declared_name(kind: int, header: str) -> str | None:
    """Extracts the identifier declared by a namespace-level item."""
    patterns = {
        1: r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|\{|$)",
        2: r"\busing\s+([A-Za-z_][A-Za-z0-9_]*)\s*=",
        3: r"\benum\s+(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)",
        4: r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)",
        5: r"\bclass\s+([A-Za-z_][A-Za-z0-9_]*)",
    }
    pattern = patterns.get(kind)
    if not pattern:
        return None
    m = re.search(pattern, header)
    return m.group(1) if m else None


def max_referenced_kind(text: str, declared_kinds: dict[str, int]) -> int:
    """Returns the highest canonical rank of any local type/alias referenced in text, or -1."""
    highest = -1
    for prev, prev_kind in declared_kinds.items():
        if prev_kind > highest and re.search(rf"\b{re.escape(prev)}\b", text):
            highest = prev_kind
    return highest


def _is_ignored_namespace_decl(header: str) -> bool:
    ignored_prefixes = ("namespace ", "static_assert", "template class ", "template struct ", "extern ")
    return header == "namespace" or header.startswith(ignored_prefixes)


def _is_constant_decl(header: str, full: str) -> bool:
    if re.match(r"^(?:inline\s+)?constexpr\b", header):
        return True
    return bool(
        re.match(r"^(?:inline\s+)?const\b", header)
        and "=" in full
        and not re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s*(?:const)?$", header)
    )


def _classify_constant_rank(header: str, full: str, declared_kinds: dict[str, int]) -> tuple[int, str | None]:
    name = extract_declared_name(1, header)
    check_text = re.sub(rf"\b{re.escape(name)}\b", "", full, count=1) if name else full
    ref_kind = max_referenced_kind(check_text, declared_kinds)
    if ref_kind >= 3:
        return 6, name
    if ref_kind == 2:
        return 2, name
    return 1, name


def classify_namespace_decl(header: str, full: str, declared_kinds: dict[str, int]) -> tuple[int | None, str | None]:
    """Classifies a top-level declaration inside a namespace into canonical rank 0..7."""
    if _is_ignored_namespace_decl(header):
        return None, None

    has_body = "{" in full
    if not has_body and re.match(r"^(?:template\s*<[^>]+>\s*)?(?:class|struct|enum\s+class)\s+[A-Za-z_][A-Za-z0-9_]*$", header):
        return 0, None

    if re.match(r"^(?:template\s*<[^>]+>\s*)?using\s+[A-Za-z_][A-Za-z0-9_]*\s*=", header):
        name = extract_declared_name(2, header)
        ref_kind = max_referenced_kind(header.split("=", 1)[1], declared_kinds)
        return (6 if ref_kind >= 3 else 2), name

    if has_body:
        for rank, pat in (
            (3, r"^enum\s+(?:class\s+)?[A-Za-z_][A-Za-z0-9_]*\b"),
            (4, r"^(?:template\s*<[^>]+>\s*)?struct\s+[A-Za-z_][A-Za-z0-9_]*\b"),
            (5, r"^(?:template\s*<[^>]+>\s*)?class\s+[A-Za-z_][A-Za-z0-9_]*\b"),
        ):
            if re.match(pat, header):
                return rank, extract_declared_name(rank, header)

    if _is_constant_decl(header, full):
        return _classify_constant_rank(header, full, declared_kinds)

    return (7, None) if "(" in header else (None, None)


def _classify_class_item_rank(stmt: str) -> int:
    if stmt.startswith("friend "):
        return 0
    if re.match(r"^(?:template\s*<[^>]+>\s*)?(?:class|struct|enum)\b", stmt) or stmt.startswith("using "):
        return 1
    if re.match(r"^(?:inline\s+)?(?:static\s+)?constexpr\b", stmt):
        return 2
    no_templates = strip_template_args(stmt)
    before_brace = no_templates.split("{", 1)[0]
    op_masked = re.sub(r"\boperator\s*[=<>!+\-*/%&|^~]+", "operator_fn", before_brace)
    before_assign = op_masked.split("=", 1)[0]
    return 4 if "(" in before_assign else 3


def _record_class_statement(
    rel_path: str,
    class_name: str,
    raw: str,
    line_no: int,
    state: _ClassScanState,
    violations: list[Violation],
) -> None:
    stmt = re.sub(r"\s+", " ", raw).strip()
    if not stmt or stmt.startswith(("static_assert", "#")):
        return
    if state.max_access_rank == -1:
        state.max_access_rank = ACCESS_RANK[state.access]

    rank = _classify_class_item_rank(stmt)
    if rank < state.max_item_rank:
        violations.append(
            Violation(
                file=rel_path,
                line=line_no,
                scope=f"{class_name}::{state.access}",
                message=f"{CLASS_ITEM_NAMES[rank]} appears after {state.max_item_name}",
            )
        )
    elif rank > state.max_item_rank:
        state.max_item_rank = rank
        state.max_item_name = CLASS_ITEM_NAMES[rank]


def _handle_access_label(
    rel_path: str,
    class_name: str,
    candidate: str,
    line_no: int,
    state: _ClassScanState,
    seen_explicit: set[str],
    violations: list[Violation],
) -> None:
    new_rank = ACCESS_RANK[candidate]
    if new_rank < state.max_access_rank:
        violations.append(
            Violation(
                file=rel_path,
                line=line_no,
                scope=class_name,
                message=f"'{candidate}:' appears after a more restricted access section",
            )
        )
    elif candidate in seen_explicit:
        violations.append(
            Violation(
                file=rel_path,
                line=line_no,
                scope=class_name,
                message=f"duplicate '{candidate}:' section",
            )
        )
    seen_explicit.add(candidate)
    state.access = candidate
    state.max_access_rank = max(state.max_access_rank, new_rank)
    state.max_item_rank = -1
    state.max_item_name = ""


def _statement_line(body: str, body_start_line: int, stmt_start: int, raw: str) -> int:
    return body_start_line + body[: stmt_start + (len(raw) - len(raw.lstrip()))].count("\n")


def check_class_body(
    rel_path: str,
    class_name: str,
    body: str,
    body_start_line: int,
    default_access: str,
) -> list[Violation]:
    """Checks access section order and item order inside a single class/struct body."""
    violations: list[Violation] = []
    state = _ClassScanState(access=default_access)
    seen_explicit: set[str] = set()

    n = len(body)
    i = depth = paren_depth = stmt_start = 0

    while i < n:
        c = body[i]
        depth, paren_depth = _update_nesting(c, depth, paren_depth)
        if depth == 0 and paren_depth == 0 and c in ("}", ";"):
            end_idx = _advance_past_optional_semicolon(body, i) if c == "}" else i + 1
            raw = body[stmt_start:end_idx]
            line_no = _statement_line(body, body_start_line, stmt_start, raw)
            _record_class_statement(rel_path, class_name, raw, line_no, state, violations)
            stmt_start = i = end_idx
            continue
        if depth == 0 and paren_depth == 0 and c == ":" and (i + 1 >= n or body[i + 1] != ":"):
            candidate = body[stmt_start:i].strip()
            if candidate in ACCESS_RANK:
                _handle_access_label(
                    rel_path, class_name, candidate, body_start_line + body[:i].count("\n"), state, seen_explicit, violations
                )
                stmt_start = i + 1
        i += 1

    return violations


def _check_nested_type_body(rel_path: str, kind: int | None, name: str | None, line_no: int, full: str) -> list[Violation]:
    if kind not in (4, 5) or "{" not in full:
        return []
    brace_idx = full.find("{")
    close_idx = find_matching_brace(full, brace_idx)
    if close_idx == -1:
        return []
    body = full[brace_idx + 1 : close_idx]
    body_line = line_no + full[: brace_idx + 1].count("\n")
    return check_class_body(rel_path, name or "anonymous", body, body_line, "private" if kind == 5 else "public")


def check_namespace_block(
    rel_path: str,
    scope_name: str,
    block: str,
    base_line: int,
    check_namespace_order: bool = True,
) -> list[Violation]:
    """Checks declaration order inside a namespace block and recurses into its classes/structs."""
    violations: list[Violation] = []
    declared_kinds: dict[str, int] = {}
    max_rank = -1
    max_rank_name = ""

    for line_no, header, full in split_top_level_decls(block, base_line):
        kind, name = classify_namespace_decl(header, full, declared_kinds)
        if kind is not None and check_namespace_order:
            if kind < max_rank:
                violations.append(
                    Violation(
                        file=rel_path,
                        line=line_no,
                        scope=scope_name,
                        message=f"{NAMESPACE_KIND_NAMES[kind]} '{name or header[:40]}' appears after {max_rank_name}",
                    )
                )
            elif kind > max_rank:
                max_rank = kind
                max_rank_name = NAMESPACE_KIND_NAMES[kind]

        if name and kind in (2, 3, 4, 5, 6):
            declared_kinds[name] = kind
        violations.extend(_check_nested_type_body(rel_path, kind, name, line_no, full))

    return violations


def _check_anonymous_namespaces(rel_path: str, ns_body: str, ns_line: int) -> list[Violation]:
    violations: list[Violation] = []
    anon_re = re.compile(r"\bnamespace\s*\{")
    apos = 0
    while True:
        am = anon_re.search(ns_body, apos)
        if not am:
            break
        a_brace = am.end() - 1
        a_close = find_matching_brace(ns_body, a_brace)
        if a_close == -1:
            break
        a_body = ns_body[a_brace + 1 : a_close]
        a_line = ns_line + ns_body[: a_brace + 1].count("\n")
        violations.extend(check_namespace_block(rel_path, "anonymous namespace", a_body, a_line, True))
        apos = a_close + 1
    return violations


def check_file(rel_path: str, text: str) -> list[Violation]:
    """Checks canonical ordering rules on one C++ header or source file."""
    clean = strip_comments_and_strings(text)
    violations: list[Violation] = []
    is_header = rel_path.endswith(".h")

    ns_re = re.compile(r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{")
    pos = 0
    while True:
        m = ns_re.search(clean, pos)
        if not m:
            break
        brace = m.end() - 1
        close = find_matching_brace(clean, brace)
        if close == -1:
            break
        ns_name = m.group(1)
        ns_body = clean[brace + 1 : close]
        ns_line = clean[: brace + 1].count("\n") + 1

        if is_header:
            violations.extend(check_namespace_block(rel_path, f"namespace {ns_name}", ns_body, ns_line, True))
        else:
            violations.extend(_check_anonymous_namespaces(rel_path, ns_body, ns_line))
            violations.extend(check_namespace_block(rel_path, f"namespace {ns_name}", ns_body, ns_line, False))

        pos = close + 1

    return violations


def collect_target_files(root_dir: Path, target: str) -> list[tuple[str, str]]:
    """Collects sorted (rel_path, content) pairs for a target."""
    files: list[tuple[str, str]] = []
    for rel_root in TARGETS[target]:
        base = root_dir / rel_root
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            rel = path.relative_to(root_dir).as_posix()
            if any(seg in f"/{rel}" for seg in EXCLUDED_SEGMENTS):
                continue
            files.append((rel, path.read_text(encoding="utf-8", errors="replace")))
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check canonical C++ file and class declaration order.")
    parser.add_argument("target", choices=sorted(TARGETS.keys()), help="Configured target scope to check.")
    args = parser.parse_args(argv)

    files = collect_target_files(PROJECT_ROOT, args.target)
    all_violations: list[Violation] = []
    for rel, text in files:
        all_violations.extend(check_file(rel, text))

    if all_violations:
        for v in all_violations:
            print(v.format())
        print(f"\nFound {len(all_violations)} ordering violation(s) across {len({v.file for v in all_violations})} file(s).")
        return 1

    print(f"OK: 0 ordering violations across {len(files)} files in '{args.target}'.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

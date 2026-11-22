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
    .venv/bin/python tools/file_order_check.py nodalys --verbose
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

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


def strip_comments_and_strings(code: str) -> str:
    """Replaces comments and string/char literals with spaces while keeping newlines intact."""
    out: list[str] = []
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        nxt = code[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and code[i] != "\n":
                out.append(" ")
                i += 1
        elif ch == "/" and nxt == "*":
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n:
                if code[i] == "\n":
                    out.append("\n")
                    i += 1
                elif code[i] == "*" and i + 1 < n and code[i + 1] == "/":
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    break
                else:
                    out.append(" ")
                    i += 1
        elif ch == "R" and nxt == '"' and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
            # Raw string literal R"delim(...)delim"
            paren = code.find("(", i + 2)
            if paren != -1:
                delim = code[i + 2 : paren]
                end_marker = f"){delim}\""
                end_idx = code.find(end_marker, paren + 1)
                if end_idx != -1:
                    span = code[i : end_idx + len(end_marker)]
                    out.append("".join("\n" if c == "\n" else " " for c in span))
                    i = end_idx + len(end_marker)
                    continue
            out.append(ch)
            i += 1
        elif ch in ('"', "'"):
            quote = ch
            out.append(" ")
            i += 1
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
                    i += 1
                    break
                out.append(" ")
                i += 1
                if c == quote:
                    break
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
        if text[i] == "<":
            prefix = "".join(out).rstrip()
            if not re.search(r"\boperator\s*$", prefix):
                depth = 1
                j = i + 1
                while j < n and depth > 0:
                    if text[j] == "<":
                        depth += 1
                    elif text[j] == ">":
                        depth -= 1
                    j += 1
                if depth == 0:
                    i = j
                    continue
        out.append(text[i])
        i += 1
    return "".join(out)


def split_top_level_decls(block: str, base_line: int) -> list[tuple[int, str, str]]:
    """Splits a namespace block into top-level declarations.

    Returns a list of (start_line, header_text, full_text).
    """
    decls: list[tuple[int, str, str]] = []
    n = len(block)
    i = 0
    depth = 0
    paren_depth = 0
    start = 0

    def emit(end_idx: int, has_brace: bool) -> None:
        nonlocal start
        raw = block[start:end_idx]
        # Skip leading whitespace / preprocessor lines
        stripped = raw.lstrip()
        if not stripped:
            start = end_idx
            return
        lead_ws = len(raw) - len(stripped)
        line_no = base_line + block[: start + lead_ws].count("\n")
        # Ignore standalone preprocessor lines
        clean_lines = [ln for ln in stripped.splitlines() if not ln.lstrip().startswith("#")]
        full = "\n".join(clean_lines).strip()
        if full:
            header = full.split("{", 1)[0].strip() if has_brace else full.rstrip(";").strip()
            decls.append((line_no, re.sub(r"\s+", " ", header), full))
        start = end_idx

    while i < n:
        c = block[i]
        if depth == 0 and paren_depth == 0 and c == "#":
            # Skip preprocessor directive line
            nl = block.find("\n", i)
            if nl == -1:
                break
            i = nl + 1
            if not block[start:i].strip() or block[start:i].lstrip().startswith("#"):
                start = i
            continue
        if c == "(":
            paren_depth += 1
        elif c == ")":
            paren_depth = max(0, paren_depth - 1)
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and paren_depth == 0:
                # Advance past trailing ';' if present (for struct/class/enum/constexpr brace-init)
                j = i + 1
                while j < n and block[j] in " \t\r\n":
                    j += 1
                if j < n and block[j] == ";":
                    emit(j + 1, True)
                    i = j + 1
                    continue
                emit(i + 1, True)
                i += 1
                continue
        elif depth == 0 and paren_depth == 0 and c == ";":
            emit(i + 1, False)
            i += 1
            continue
        i += 1

    return decls


def extract_declared_name(kind: int, header: str) -> str | None:
    """Extracts the identifier declared by a namespace-level item."""
    if kind == 1:
        m = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|\{|$)", header)
        return m.group(1) if m else None
    if kind == 2:
        m = re.search(r"\busing\s+([A-Za-z_][A-Za-z0-9_]*)\s*=", header)
        return m.group(1) if m else None
    if kind == 3:
        m = re.search(r"\benum\s+(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)", header)
        return m.group(1) if m else None
    if kind in (4, 5):
        m = re.search(r"\b(?:struct|class)\s+([A-Za-z_][A-Za-z0-9_]*)", header)
        return m.group(1) if m else None
    return None


def max_referenced_kind(text: str, declared_kinds: dict[str, int]) -> int:
    """Returns the highest canonical rank of any local type/alias referenced in text, or -1."""
    highest = -1
    for prev, prev_kind in declared_kinds.items():
        if prev_kind > highest and re.search(rf"\b{re.escape(prev)}\b", text):
            highest = prev_kind
    return highest


def classify_namespace_decl(header: str, full: str, declared_kinds: dict[str, int]) -> tuple[int | None, str | None]:
    """Classifies a top-level declaration inside a namespace into canonical rank 0..7."""
    # Ignore nested namespaces or static_assert / template instantiations
    if header.startswith("namespace ") or header == "namespace" or header.startswith("static_assert"):
        return None, None
    if header.startswith("template class ") or header.startswith("template struct ") or header.startswith("extern "):
        return None, None

    has_body = "{" in full

    # Forward declaration: class Foo; / struct Bar; / enum class Baz;
    if not has_body and re.match(r"^(?:template\s*<[^>]+>\s*)?(?:class|struct|enum\s+class)\s+[A-Za-z_][A-Za-z0-9_]*$", header):
        return 0, None

    # Type alias
    if re.match(r"^(?:template\s*<[^>]+>\s*)?using\s+[A-Za-z_][A-Za-z0-9_]*\s*=", header):
        name = extract_declared_name(2, header)
        rhs = header.split("=", 1)[1]
        ref_kind = max_referenced_kind(rhs, declared_kinds)
        if ref_kind >= 3:
            return 6, name
        return 2, name

    # Enum definition
    if has_body and re.match(r"^enum\s+(?:class\s+)?[A-Za-z_][A-Za-z0-9_]*\b", header):
        return 3, extract_declared_name(3, header)

    # Struct definition
    if has_body and re.match(r"^(?:template\s*<[^>]+>\s*)?struct\s+[A-Za-z_][A-Za-z0-9_]*\b", header):
        return 4, extract_declared_name(4, header)

    # Class definition
    if has_body and re.match(r"^(?:template\s*<[^>]+>\s*)?class\s+[A-Za-z_][A-Za-z0-9_]*\b", header):
        return 5, extract_declared_name(5, header)

    # Constant (constexpr / inline constexpr / const variable without '(' function signature)
    if re.match(r"^(?:inline\s+)?constexpr\b", header) or (
        re.match(r"^(?:inline\s+)?const\b", header) and "=" in full and not re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s*(?:const)?$", header)
    ):
        name = extract_declared_name(1, header)
        check_text = full
        if name:
            # Remove the constant's own name before checking dependency on earlier local types.
            check_text = re.sub(rf"\b{re.escape(name)}\b", "", full, count=1)
        ref_kind = max_referenced_kind(check_text, declared_kinds)
        if ref_kind >= 3:
            return 6, name
        if ref_kind == 2:
            # A constant whose type is a local type alias (e.g. `constexpr NodeId GROUND_NODE = 0;`)
            # sits alongside type aliases, still before enums/structs/classes.
            return 2, name
        return 1, name

    # Free function declaration or definition
    if "(" in header:
        return 7, None

    return None, None


def check_class_body(
    rel_path: str,
    class_name: str,
    body: str,
    body_start_line: int,
    default_access: str,
) -> list[Violation]:
    """Checks access section order and item order inside a single class/struct body."""
    violations: list[Violation] = []
    access = default_access
    max_access_rank = -1
    seen_explicit_access: set[str] = set()
    max_item_rank = -1
    max_item_name = ""

    n = len(body)
    i = 0
    depth = 0
    paren_depth = 0
    stmt_start = 0

    def check_statement(raw: str, line_no: int) -> None:
        nonlocal max_access_rank, max_item_rank, max_item_name
        stmt = re.sub(r"\s+", " ", raw).strip()
        if not stmt or stmt.startswith("static_assert") or stmt.startswith("#"):
            return

        if max_access_rank == -1:
            max_access_rank = ACCESS_RANK[access]

        if stmt.startswith("friend "):
            rank = 0
        elif re.match(r"^(?:template\s*<[^>]+>\s*)?(?:class|struct|enum)\b", stmt) or stmt.startswith("using "):
            rank = 1
        elif re.match(r"^(?:static\s+)?constexpr\b", stmt) or re.match(r"^inline\s+static\s+constexpr\b", stmt):
            rank = 2
        else:
            # Remove template argument lists before inspecting for '(' vs '='.
            # This distinguishes `void Set(std::function<void()> cb)` (method) from
            # `std::function<void()> m_Cb;` (data member) and handles default `= {}` arguments.
            no_templates = strip_template_args(stmt)
            # Check for '(' before any '{' or '=' (ignoring operator== / operator=).
            before_brace = no_templates.split("{", 1)[0]
            op_masked = re.sub(r"\boperator\s*[=<>!+\-*/%&|^~]+", "operator_fn", before_brace)
            before_assign = op_masked.split("=", 1)[0]
            is_method = "(" in before_assign
            rank = 4 if is_method else 3

        # In structs (default public), data members naturally precede operator== / helper methods (3 -> 4),
        # which already matches our rank ordering 0..4!
        if rank < max_item_rank:
            # Allow a nested type or constant only before members/methods; flag regressions clearly.
            violations.append(
                Violation(
                    file=rel_path,
                    line=line_no,
                    scope=f"{class_name}::{access}",
                    message=f"{CLASS_ITEM_NAMES[rank]} appears after {max_item_name}",
                )
            )
        elif rank > max_item_rank:
            max_item_rank = rank
            max_item_name = CLASS_ITEM_NAMES[rank]

    while i < n:
        c = body[i]
        if c == "(":
            paren_depth += 1
        elif c == ")":
            paren_depth = max(0, paren_depth - 1)
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and paren_depth == 0:
                j = i + 1
                while j < n and body[j] in " \t\r\n":
                    j += 1
                end_idx = j + 1 if (j < n and body[j] == ";") else i + 1
                raw = body[stmt_start:end_idx]
                lead = len(raw) - len(raw.lstrip())
                check_statement(raw, body_start_line + body[: stmt_start + lead].count("\n"))
                stmt_start = end_idx
                i = end_idx
                continue
        elif depth == 0 and paren_depth == 0 and c == ";":
            raw = body[stmt_start : i + 1]
            lead = len(raw) - len(raw.lstrip())
            check_statement(raw, body_start_line + body[: stmt_start + lead].count("\n"))
            stmt_start = i + 1
        elif depth == 0 and paren_depth == 0 and c == ":" and (i + 1 >= n or body[i + 1] != ":"):
            candidate = body[stmt_start:i].strip()
            if candidate in ACCESS_RANK:
                new_rank = ACCESS_RANK[candidate]
                line_no = body_start_line + body[:i].count("\n")
                if new_rank < max_access_rank:
                    violations.append(
                        Violation(
                            file=rel_path,
                            line=line_no,
                            scope=class_name,
                            message=f"'{candidate}:' appears after a more restricted access section",
                        )
                    )
                elif candidate in seen_explicit_access:
                    violations.append(
                        Violation(
                            file=rel_path,
                            line=line_no,
                            scope=class_name,
                            message=f"duplicate '{candidate}:' section",
                        )
                    )
                seen_explicit_access.add(candidate)
                access = candidate
                max_access_rank = max(max_access_rank, new_rank)
                max_item_rank = -1
                max_item_name = ""
                stmt_start = i + 1
        i += 1

    return violations


def check_namespace_block(
    rel_path: str,
    scope_name: str,
    block: str,
    base_line: int,
    check_namespace_order: bool = True,
) -> list[Violation]:
    """Checks declaration order inside a namespace block and recurses into its classes/structs."""
    violations: list[Violation] = []
    decls = split_top_level_decls(block, base_line)

    declared_kinds: dict[str, int] = {}
    max_rank = -1
    max_rank_name = ""

    for line_no, header, full in decls:
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

        # Always inspect internal ordering of classes and structs.
        if kind in (4, 5) and "{" in full:
            brace_idx = full.find("{")
            close_idx = find_matching_brace(full, brace_idx)
            if close_idx != -1:
                body = full[brace_idx + 1 : close_idx]
                body_line = line_no + full[: brace_idx + 1].count("\n")
                default_access = "private" if kind == 5 else "public"
                violations.extend(check_class_body(rel_path, name or "anonymous", body, body_line, default_access))

    return violations


def check_file(rel_path: str, text: str) -> list[Violation]:
    """Checks canonical ordering rules on one C++ header or source file."""
    clean = strip_comments_and_strings(text)
    violations: list[Violation] = []
    is_header = rel_path.endswith(".h")

    # Find top-level named namespaces (e.g., namespace Nodalys { ... }).
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
            # In .cpp files, check all anonymous namespace(s) for full canonical order, and check any internal classes
            # defined in the named namespace (e.g. CircuitBuilderImpl).
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

#!/usr/bin/env python3
"""Generates a self-contained, interactive HTML class diagram and live audit for a C++ library.

The script parses C++ headers and sources using only the Python standard library, extracts classes,
structs, inheritance, composition/aggregation/usage relationships, module include graphs (including
cycle detection), and live architectural health metrics aligned with the project guidelines.

Usage:
    .venv/bin/python tools/class_diagram.py nodalys
    .venv/bin/python tools/class_diagram.py nodalys --output path/to/out.html
    .venv/bin/python tools/class_diagram.py nodalys --check
    .venv/bin/python tools/class_diagram.py nodalys --json-only
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_PATH = Path(__file__).resolve().parent / "class_diagram_template.html"

SOURCE_SUFFIXES = (".h", ".cpp")
EXCLUDED_SEGMENTS = ("/tests/", "/vendor/", "/build/", "/cmake-build-")

PALETTE = [
    "#58a6ff",
    "#39c5cf",
    "#3fb950",
    "#f0883e",
    "#bc8cff",
    "#f778ba",
    "#d29922",
    "#56d364",
    "#79c0ff",
    "#a5d6ff",
    "#ffa657",
    "#d2a8ff",
    "#8b949e",
]

# Declarative targets so app-core and app-gui can be added as independent diagrams later.
TARGETS: dict[str, dict[str, Any]] = {
    "nodalys": {
        "title": "Nodalys — Class Diagram & Live Audit",
        "subtitle": "C++23 simulation engine (nodalys/) + imcsim bridge (app-core/backends/nodalys/)",
        "output": "docs/nodalys/class_diagram.html",
        "roots": [
            ("nodalys/include", "public-api"),
            ("nodalys/src", None),
            ("app-core/backends/nodalys", "imcsim-backend"),
        ],
        "public_header_prefix": "nodalys/",
        "module_order": [
            "imcsim-backend",
            "public-api",
            "cli",
            "export",
            "bridge",
            "api",
            "parser",
            "expression",
            "analyses",
            "newton",
            "compiler",
            "devices",
            "mna",
            "linear",
            "text",
        ],
        "module_colors": {
            "imcsim-backend": "#79c0ff",
            "public-api": "#58a6ff",
            "api": "#39c5cf",
            "compiler": "#3fb950",
            "devices": "#f0883e",
            "mna": "#bc8cff",
            "linear": "#a371f7",
            "newton": "#ff7b72",
            "analyses": "#f778ba",
            "expression": "#d29922",
            "parser": "#56d364",
            "export": "#8b949e",
            "bridge": "#76e3ea",
            "cli": "#c9d1d9",
            "text": "#7ee787",
        },
        # Public headers whose implementation lives in a specific internal module, used to uncover indirect cycles.
        "public_header_owners": {
            "nodalys/expression.h": "expression",
            "nodalys/netlist_parser.h": "parser",
            "nodalys/subcircuit_text.h": "parser",
            "nodalys/exporters.h": "export",
            "nodalys/logic_bridge.h": "bridge",
            "nodalys/circuit.h": "api",
            "nodalys/circuit_builder.h": "api",
        },
    }
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
TYPE_DECL_RE = re.compile(
    r"^[ \t]*(template\s*<[^>{;]+>\s*)?(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s+final)?(?:\s*:\s*([^{;]+))?\s*\{",
    re.MULTILINE,
)
FUNC_DEF_RE = re.compile(
    r"^[ \t]*([A-Za-z_~][^\n;{}]*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[^{]+)?)\s*\{",
    re.MULTILINE,
)


@dataclass
class SourceFile:
    rel_path: str
    module: str
    text: str
    lines: int


def _skip_line_comment(code: str, start: int) -> int:
    n = len(code)
    i = start
    while i < n and code[i] != "\n":
        i += 1
    return i


def _skip_block_comment(code: str, start: int, out: list[str]) -> int:
    n = len(code)
    i = start + 2
    while i < n:
        if code[i] == "\n":
            out.append("\n")
        if code[i] == "*" and i + 1 < n and code[i + 1] == "/":
            return i + 2
        i += 1
    return i


def _skip_quoted_literal(code: str, start: int, quote: str, out: list[str]) -> int:
    n = len(code)
    out.append(quote)
    i = start + 1
    while i < n:
        c = code[i]
        if c == "\\":
            i += 2
            continue
        if c == "\n":
            out.append("\n")
            return i + 1
        if c == quote:
            out.append(quote)
            return i + 1
        i += 1
    return i


def strip_comments_preserve_lines(code: str) -> str:
    """Removes C/C++ comments and string literals while preserving line numbers."""
    out: list[str] = []
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        nxt = code[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            i = _skip_line_comment(code, i)
        elif ch == "/" and nxt == "*":
            i = _skip_block_comment(code, i, out)
        elif ch in ('"', "'"):
            i = _skip_quoted_literal(code, i, ch, out)
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def find_matching_brace(text: str, open_pos: int) -> int:
    """Returns the index of the closing brace matching open_pos, or open_pos if unmatched."""
    depth = 0
    for idx in range(open_pos, len(text)):
        c = text[idx]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return idx
    return open_pos


def _extract_block_brief(lines: list[str]) -> str:
    if not lines[-1].strip().endswith("*/"):
        return ""
    block_lines: list[str] = []
    for line in reversed(lines):
        block_lines.append(line)
        if "/*" in line:
            break
    block = "\n".join(reversed(block_lines))
    if "/**" not in block:
        return ""
    brief_match = re.search(r"@brief\s+(.*?)(?=\n\s*\*\s*@|\*/)", block, re.DOTALL)
    if not brief_match:
        return ""
    cleaned = re.sub(r"\n\s*\*\s*", " ", brief_match.group(1)).strip()
    return re.sub(r"\s+", " ", cleaned)


def _extract_slash_brief(lines: list[str]) -> str:
    slash_lines: list[str] = []
    for line in reversed(lines):
        stripped = line.strip()
        if not stripped.startswith("///"):
            break
        slash_lines.append(stripped.lstrip("/").strip())
    return " ".join(reversed(slash_lines))


def extract_preceding_brief(raw_text: str, start_line: int) -> str:
    """Extracts the Doxygen @brief or /// comment immediately above start_line (1-based)."""
    lines = raw_text.splitlines()[: max(0, start_line - 1)]
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines:
        return ""
    return _extract_block_brief(lines) or _extract_slash_brief(lines)


VISIBILITY_SYMBOLS = {"public": "+", "protected": "#", "private": "-"}


def _flush_statement(buf: list[str], statements: list[tuple[str, str]], visibility: str) -> None:
    stmt = "".join(buf).strip()
    if stmt:
        statements.append((visibility, stmt))
    buf.clear()


def _collect_body_statements(body: str, default_visibility: str) -> list[tuple[str, str]]:
    """Collects top-level (depth == 0) statements inside a class or struct body."""
    visibility = default_visibility
    statements: list[tuple[str, str]] = []
    buf: list[str] = []
    depth = 0
    n = len(body)
    for i, c in enumerate(body):
        if c == "{":
            depth += 1
            buf.append(c)
        elif c == "}":
            depth = max(0, depth - 1)
            buf.append(c)
            if depth == 0:
                _flush_statement(buf, statements, visibility)
        elif depth == 0 and c == ";":
            _flush_statement(buf, statements, visibility)
        elif depth == 0 and c == ":" and (i + 1 >= n or body[i + 1] != ":"):
            candidate = "".join(buf).strip()
            if candidate in VISIBILITY_SYMBOLS:
                visibility = candidate
                buf.clear()
            else:
                buf.append(c)
        else:
            buf.append(c)
    return statements


def _skip_std_function_args(after_fn: str) -> str:
    """Returns the substring after the balanced angle brackets of std::function<...>."""
    angle_depth = 1
    pos = 0
    while pos < len(after_fn) and angle_depth > 0:
        if after_fn[pos] == "<":
            angle_depth += 1
        elif after_fn[pos] == ">":
            angle_depth -= 1
        pos += 1
    return after_fn[pos:]


def _is_method_declaration(header_part: str) -> bool:
    if "std::function<" in header_part:
        tail = _skip_std_function_args(header_part.split("std::function<", 1)[1])
        return "(" in tail
    return "(" in header_part and not re.match(r"^[A-Za-z0-9_:<>,\s*&]+\s+[A-Za-z_][A-Za-z0-9_]*\s*=", header_part)


def _classify_member_statement(vis: str, raw_stmt: str) -> tuple[str, str, int, int] | None:
    """Classifies a top-level class body statement into ('type'|'method'|'field', text, virtuals, pure_virtuals)."""
    stmt = re.sub(r"\s+", " ", raw_stmt).strip()
    if not stmt or stmt.startswith("friend ") or stmt.startswith("static_assert"):
        return None
    if stmt.startswith("using ") or re.match(r"^(?:template\s*<[^>]+>\s*)?(class|struct|enum)\b", stmt):
        return ("type", "", 0, 0)

    header_part = stmt.split("{", 1)[0].strip()
    sym = VISIBILITY_SYMBOLS[vis]
    if _is_method_declaration(header_part):
        is_virtual = 1 if re.search(r"\bvirtual\b", header_part) else 0
        is_pure = 1 if re.search(r"=\s*0\s*$", header_part) else 0
        return ("method", f"{sym} {header_part}", is_virtual, is_pure)

    clean_field = re.sub(r"\s*=\s*.*$", "", header_part).strip()
    if not clean_field:
        return None
    return ("field", f"{sym} {clean_field}", 0, 0)


def _is_private_order_inverted(private_events: list[str]) -> bool:
    # CLAUDE.md rule: in private:, nested types first, then members, then private methods.
    first_priv_method = next((idx for idx, kind in enumerate(private_events) if kind == "method"), None)
    last_priv_field = next(
        (idx for idx in range(len(private_events) - 1, -1, -1) if private_events[idx] == "field"),
        None,
    )
    return first_priv_method is not None and last_priv_field is not None and first_priv_method < last_priv_field


def parse_member_sections(body: str, default_visibility: str) -> tuple[list[str], list[str], dict[str, Any]]:
    """Splits a class/struct body into top-level fields and methods with visibility markers."""
    fields: list[str] = []
    methods: list[str] = []
    virtual_count = 0
    pure_virtual_count = 0
    private_events: list[str] = []

    for vis, raw_stmt in _collect_body_statements(body, default_visibility):
        parsed = _classify_member_statement(vis, raw_stmt)
        if parsed is None:
            continue
        kind, text, v_inc, pv_inc = parsed
        virtual_count += v_inc
        pure_virtual_count += pv_inc
        if kind == "method":
            methods.append(text)
        elif kind == "field":
            fields.append(text)
        if vis == "private":
            private_events.append(kind)

    stats = {
        "virtual_count": virtual_count,
        "pure_virtual_count": pure_virtual_count,
        "private_order_inverted": _is_private_order_inverted(private_events),
    }
    return fields, methods, stats


def _parse_base_classes(bases_raw: str) -> list[str]:
    bases: list[str] = []
    for base in bases_raw.split(","):
        cleaned = re.sub(r"\b(public|protected|private|virtual)\b", "", base).strip()
        cleaned = cleaned.split("<", 1)[0].split("::")[-1].strip()
        if cleaned:
            bases.append(cleaned)
    return bases


def _classify_stereotype(kind: str, tmpl: str, rel_path: str, is_pod: bool, pure_virtual_count: int) -> str:
    if pure_virtual_count > 0:
        return f"«abstract {kind}»"
    if tmpl:
        return f"«template {kind}»"
    if rel_path.endswith(".cpp"):
        return f"«internal {kind}»"
    if is_pod:
        return "«data struct»"
    return f"«{kind}»"


def _build_type_node(src: SourceFile, clean: str, match: re.Match[str], end_pos: int) -> dict[str, Any]:
    tmpl = (match.group(1) or "").strip()
    kind = match.group(2).strip()
    name = match.group(3).strip()
    bases = _parse_base_classes((match.group(4) or "").strip())

    start_line = clean[: match.start()].count("\n") + 1
    end_line = clean[:end_pos].count("\n") + 1
    body = clean[match.end() : end_pos]
    default_vis = "private" if kind == "class" else "public"
    fields, methods, stats = parse_member_sections(body, default_vis)
    brief = extract_preceding_brief(src.text, start_line)

    non_eq_methods = [mt for mt in methods if "operator==" not in mt and "operator<=>" not in mt]
    is_pod = kind == "struct" and len(non_eq_methods) == 0 and len(bases) == 0
    stereotype = _classify_stereotype(kind, tmpl, src.rel_path, is_pod, stats["pure_virtual_count"])

    return {
        "id": name,
        "kind": kind,
        "stereotype": stereotype,
        "template": tmpl,
        "bases": bases,
        "module": src.module,
        "file": src.rel_path,
        "line": start_line,
        "end_line": end_line,
        "lines": end_line - start_line + 1,
        "summary": brief or f"Defined in {src.rel_path}:{start_line}.",
        "fields": fields,
        "methods": methods,
        "is_pod": is_pod,
        "virtual_count": stats["virtual_count"],
        "pure_virtual_count": stats["pure_virtual_count"],
        "private_order_inverted": stats["private_order_inverted"],
        "alerts": [],
        "check_ids": [],
    }


def extract_types_from_file(src: SourceFile) -> list[dict[str, Any]]:
    """Extracts top-level C++ classes and structs from a single source file."""
    clean = strip_comments_preserve_lines(src.text)
    results: list[dict[str, Any]] = []

    # Track byte ranges of already-captured outer types to skip nested structs/classes.
    captured_ranges: list[tuple[int, int]] = []
    for match in TYPE_DECL_RE.finditer(clean):
        if any(start < match.start() < end for start, end in captured_ranges):
            continue
        brace_pos = match.end() - 1
        end_pos = find_matching_brace(clean, brace_pos)
        if end_pos == brace_pos:
            continue
        captured_ranges.append((match.start(), end_pos))
        results.append(_build_type_node(src, clean, match, end_pos))
    return results


def _record_edge(
    edges: list[dict[str, Any]],
    seen: set[tuple[str, str, str]],
    node_ids: set[str],
    src_id: str,
    dst_id: str,
    rel_type: str,
    label: str = "",
) -> None:
    if src_id not in node_ids or dst_id not in node_ids:
        return
    if src_id == dst_id and rel_type != "agg":
        return
    key = (src_id, dst_id, rel_type)
    if key in seen:
        return
    seen.add(key)
    edges.append({"from": src_id, "to": dst_id, "type": rel_type, "label": label})


def _classify_field_relation(decl: str, src_id: str, target: str) -> tuple[str, str] | None:
    if target == src_id:
        return ("agg", "self-ref") if ("*" in decl or "&" in decl) else None
    escaped = re.escape(target)
    if re.search(rf"std::(?:unique|shared)_ptr\s*<\s*(?:const\s+)?{escaped}\b", decl):
        return ("compose", "smart_ptr")
    if re.search(rf"\b{escaped}\s*[*&]", decl) or re.search(
        rf"std::(?:vector|map|span)\s*<[^>]*\b{escaped}\s*\*", decl
    ):
        return ("agg", "")
    return ("compose", "")


def _infer_field_edges(
    node: dict[str, Any],
    node_ids: set[str],
    ident_re: re.Pattern[str],
    nested_ref_re: re.Pattern[str],
    edges: list[dict[str, Any]],
    seen: set[tuple[str, str, str]],
    strong_links: set[tuple[str, str]],
) -> None:
    src_id = node["id"]
    for field in node["fields"]:
        decl = field[2:]
        # Detect sibling nested type coupling like VoltageSourceDevice::Waveform
        for nm in nested_ref_re.finditer(decl):
            target = nm.group(1)
            if target != src_id and target not in node["bases"]:
                _record_edge(edges, seen, node_ids, src_id, target, "smell", f"uses ::{nm.group(2)}")

        for tm in ident_re.finditer(decl):
            target = tm.group(1)
            rel = _classify_field_relation(decl, src_id, target)
            if rel is not None:
                _record_edge(edges, seen, node_ids, src_id, target, rel[0], rel[1])
                strong_links.add((src_id, target))


def _infer_method_edges(
    node: dict[str, Any],
    node_ids: set[str],
    ident_re: re.Pattern[str],
    edges: list[dict[str, Any]],
    seen: set[tuple[str, str, str]],
    strong_links: set[tuple[str, str]],
) -> None:
    src_id = node["id"]
    for method in node["methods"]:
        for tm in ident_re.finditer(method[2:]):
            target = tm.group(1)
            if target != src_id and (src_id, target) not in strong_links:
                _record_edge(edges, seen, node_ids, src_id, target, "uses")


def _flag_const_cast_targets(files: list[SourceFile], nodes: list[dict[str, Any]], node_ids: set[str]) -> None:
    const_cast_re = re.compile(r"const_cast\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*[*&]")
    nodes_by_id: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for node in nodes:
        nodes_by_id[node["id"]].append(node)
    for f in files:
        clean = strip_comments_preserve_lines(f.text)
        for m in const_cast_re.finditer(clean):
            target = m.group(1)
            if target in node_ids:
                for node in nodes_by_id[target]:
                    node["alerts"].append("const_cast!")


def infer_relationships(nodes: list[dict[str, Any]], files: list[SourceFile]) -> list[dict[str, Any]]:
    """Infers inheritance, composition, aggregation, usage, and smell edges between extracted nodes."""
    node_ids = {n["id"] for n in nodes}
    sorted_ids = sorted(node_ids, key=len, reverse=True)
    if not sorted_ids:
        return []

    pattern = "|".join(re.escape(i) for i in sorted_ids)
    ident_re = re.compile(rf"\b({pattern})\b")
    nested_ref_re = re.compile(rf"\b({pattern})::([A-Za-z_][A-Za-z0-9_]*)")

    seen: set[tuple[str, str, str]] = set()
    edges: list[dict[str, Any]] = []
    strong_links: set[tuple[str, str]] = set()

    for node in nodes:
        src_id = node["id"]
        for base in node["bases"]:
            if base in node_ids:
                _record_edge(edges, seen, node_ids, src_id, base, "inherit")
                strong_links.add((src_id, base))
        _infer_field_edges(node, node_ids, ident_re, nested_ref_re, edges, seen, strong_links)
        _infer_method_edges(node, node_ids, ident_re, edges, seen, strong_links)

    _flag_const_cast_targets(files, nodes, node_ids)
    return edges


def classify_include_module(
    include_path: str,
    known_modules: set[str],
    public_prefix: str,
    public_owners: dict[str, str],
) -> tuple[str | None, str | None]:
    """Returns (direct_target_module, indirect_owner_module) for an #include path."""
    if public_prefix and include_path.startswith(public_prefix):
        return "public-api", public_owners.get(include_path)
    if "/" in include_path:
        top = include_path.split("/", 1)[0]
        if top in known_modules:
            return top, None
    return None, None


def find_cycles(graph: dict[str, set[str]]) -> list[list[str]]:
    """Finds unique elementary directed cycles in a small module dependency graph."""
    cycles: list[list[str]] = []
    seen_canonical: set[tuple[str, ...]] = set()
    nodes = sorted(graph.keys())

    def dfs(start: str, current: str, path: list[str], visited: set[str]) -> None:
        for nxt in sorted(graph.get(current, ())):
            if nxt == start and len(path) >= 2:
                min_idx = min(range(len(path)), key=lambda i: path[i])
                canon = tuple(path[min_idx:] + path[:min_idx])
                if canon not in seen_canonical:
                    seen_canonical.add(canon)
                    cycles.append(list(canon) + [canon[0]])
            elif nxt not in visited and nxt >= start:
                visited.add(nxt)
                path.append(nxt)
                dfs(start, nxt, path, visited)
                path.pop()
                visited.remove(nxt)

    for start in nodes:
        dfs(start, start, [start], {start})
    return cycles


def _collect_module_includes(
    files: list[SourceFile],
    known_modules: set[str],
    public_prefix: str,
    public_owners: dict[str, str],
) -> tuple[dict[tuple[str, str], int], dict[str, set[str]], dict[tuple[str, str], list[str]]]:
    direct_counts: dict[tuple[str, str], int] = defaultdict(int)
    effective_graph: dict[str, set[str]] = defaultdict(set)
    effective_examples: dict[tuple[str, str], list[str]] = defaultdict(list)

    for f in files:
        for inc in INCLUDE_RE.findall(f.text):
            direct_mod, owner_mod = classify_include_module(inc, known_modules, public_prefix, public_owners)
            if direct_mod and direct_mod != f.module:
                direct_counts[(f.module, direct_mod)] += 1
                effective_graph[f.module].add(direct_mod)
            if owner_mod and owner_mod != f.module:
                effective_graph[f.module].add(owner_mod)
                if len(effective_examples[(f.module, owner_mod)]) < 3:
                    effective_examples[(f.module, owner_mod)].append(f"{f.rel_path} -> {inc}")

    for owner in public_owners.values():
        if "public-api" in known_modules and owner in known_modules:
            effective_graph["public-api"].add(owner)

    return direct_counts, effective_graph, effective_examples


def _build_internal_graph(effective_graph: dict[str, set[str]]) -> dict[str, set[str]]:
    internal_graph: dict[str, set[str]] = defaultdict(set)
    for src_mod, dsts in effective_graph.items():
        if src_mod == "public-api":
            continue
        for dst in dsts:
            if dst != "public-api" and dst != src_mod:
                internal_graph[src_mod].add(dst)
    return internal_graph


def _build_module_edges(
    direct_counts: dict[tuple[str, str], int],
    cycle_pairs: set[tuple[str, str]],
    effective_examples: dict[tuple[str, str], list[str]],
) -> list[dict[str, Any]]:
    edges_list: list[dict[str, Any]] = [
        {"from": src_mod, "to": dst_mod, "count": count, "cycle": (src_mod, dst_mod) in cycle_pairs}
        for (src_mod, dst_mod), count in sorted(direct_counts.items())
    ]
    for a, b in sorted(cycle_pairs):
        if (a, b) not in direct_counts:
            edges_list.append(
                {
                    "from": a,
                    "to": b,
                    "count": len(effective_examples.get((a, b), [])) or 1,
                    "cycle": True,
                    "indirect": True,
                }
            )
    return edges_list


def build_module_graph(files: list[SourceFile], target_cfg: dict[str, Any]) -> dict[str, Any]:
    """Computes module stats, include edges, and both direct and effective (through public API) cycles."""
    loc_by_mod: dict[str, int] = defaultdict(int)
    files_by_mod: dict[str, int] = defaultdict(int)
    for f in files:
        loc_by_mod[f.module] += f.lines
        files_by_mod[f.module] += 1

    known_modules = set(loc_by_mod.keys())
    public_prefix = target_cfg.get("public_header_prefix", "")
    public_owners = target_cfg.get("public_header_owners", {})

    direct_counts, effective_graph, effective_examples = _collect_module_includes(
        files, known_modules, public_prefix, public_owners
    )
    cycles = find_cycles(_build_internal_graph(effective_graph))
    cycle_pairs = {(a, b) for cyc in cycles for a, b in zip(cyc, cyc[1:])}

    mod_order = target_cfg.get("module_order", [])
    ordered_mods = [m for m in mod_order if m in known_modules] + sorted(known_modules - set(mod_order))
    colors = target_cfg.get("module_colors", {})
    modules_list = [
        {
            "id": mod,
            "files": files_by_mod[mod],
            "loc": loc_by_mod[mod],
            "color": colors.get(mod, PALETTE[idx % len(PALETTE)]),
        }
        for idx, mod in enumerate(ordered_mods)
    ]

    return {
        "modules": modules_list,
        "edges": _build_module_edges(direct_counts, cycle_pairs, effective_examples),
        "cycles": cycles,
        "cycle_examples": {
            f"{a} -> {b}": vals for (a, b), vals in sorted(effective_examples.items()) if (a, b) in cycle_pairs
        },
    }


def scan_long_functions(files: list[SourceFile], threshold: int = 40) -> list[dict[str, Any]]:
    """Finds function definitions in .cpp files exceeding threshold lines."""
    long_funcs: list[dict[str, Any]] = []
    for f in files:
        if not f.rel_path.endswith(".cpp"):
            continue
        clean = strip_comments_preserve_lines(f.text)
        for m in FUNC_DEF_RE.finditer(clean):
            line_start = clean.rfind("\n", 0, m.start()) + 1
            if m.start() - line_start > 0:
                continue
            sig = " ".join(m.group(1).split())
            if re.match(r"^(if|for|while|switch|catch|else\s+if|namespace|class|struct)\b", sig):
                continue
            start_line = clean[: m.start()].count("\n") + 1
            end_pos = find_matching_brace(clean, m.end() - 1)
            end_line = clean[:end_pos].count("\n") + 1
            length = end_line - start_line + 1
            if length > threshold:
                long_funcs.append(
                    {
                        "file": f.rel_path,
                        "line": start_line,
                        "length": length,
                        "signature": sig[:100],
                    }
                )
    long_funcs.sort(key=lambda item: (-item["length"], item["file"], item["line"]))
    return long_funcs


DYN_CAST_RE = re.compile(r"dynamic_cast\s*<\s*(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*[*&]")
NODISCARD_RE = re.compile(
    r"^[ \t]*(?:virtual\s+|static\s+)?(std::(?:expected|optional)<(?:[^<>]+|<[^<>]+>)+>)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)
VOID_PARAM_RE = re.compile(r"^\s*\(void\)\s*([a-z_][a-z0-9_]*)\s*;\s*$")
HEADER_CONST_PARAM_RE = re.compile(r"\bconst\s+(?:int|double|bool|std::size_t|float)\s+[a-z_][a-z0-9_]*\s*(?:=|,|$)")
HEADER_DECL_PARAMS_RE = re.compile(r"[A-Za-z_~][A-Za-z0-9_]*\s*\(([^;{}()]*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:=\s*0\s*)?;")
TODO_MARKER_RE = re.compile(r"\b(TODO|FIXME|HACK|XXX)\b")
HYGIENE_LINE_CHECKS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("#pragma once", re.compile(r"^\s*#pragma\s+once\b")),
    ("using namespace", re.compile(r"using namespace ")),
    ("raw allocation", re.compile(r"\b(new |delete |malloc\(|free\()")),
    ("throw", re.compile(r"\bthrow\b")),
)


@dataclass
class _AuditFindings:
    const_casts: list[str]
    dynamic_casts: list[str]
    dynamic_cast_targets: dict[str, int]
    missing_nodiscard: list[str]
    header_const_params: list[str]
    trailing_doxygen: list[str]
    ignored_params: list[str]
    hygiene_issues: list[str]


def _scan_clean_lines(f: SourceFile, clean_lines: list[str], findings: _AuditFindings) -> None:
    check_void_params = f.rel_path.endswith(".cpp") and f.module == "api"
    for idx, line in enumerate(clean_lines, 1):
        if "const_cast" in line:
            findings.const_casts.append(f"{f.rel_path}:{idx} — {line.strip()[:90]}")
        for dm in DYN_CAST_RE.finditer(line):
            findings.dynamic_casts.append(f"{f.rel_path}:{idx} — {line.strip()[:90]}")
            findings.dynamic_cast_targets[dm.group(1)] += 1
        for label, pattern in HYGIENE_LINE_CHECKS:
            if pattern.search(line):
                findings.hygiene_issues.append(f"{label} in {f.rel_path}:{idx}")
        if check_void_params:
            vm = VOID_PARAM_RE.match(line)
            if vm:
                findings.ignored_params.append(f"{f.rel_path}:{idx} — (void){vm.group(1)};")


def _scan_raw_lines(f: SourceFile, raw_lines: list[str], findings: _AuditFindings) -> None:
    for idx, raw_line in enumerate(raw_lines, 1):
        if "///<" in raw_line:
            findings.trailing_doxygen.append(f"{f.rel_path}:{idx} — {raw_line.strip()[:90]}")
        if TODO_MARKER_RE.search(raw_line):
            findings.hygiene_issues.append(f"TODO/FIXME in {f.rel_path}:{idx}")


def _scan_header_declarations(
    f: SourceFile, clean: str, clean_lines: list[str], findings: _AuditFindings
) -> None:
    del clean_lines
    for m in NODISCARD_RE.finditer(clean):
        line_start = clean.rfind("\n", 0, m.start()) + 1
        prev_start = clean.rfind("\n", 0, max(0, line_start - 1)) + 1
        if "[[nodiscard]]" not in clean[prev_start : m.start()]:
            line_no = clean[: m.start()].count("\n") + 1
            findings.missing_nodiscard.append(f"{f.rel_path}:{line_no} — {m.group(1)} {m.group(2)}()")
    for m in HEADER_DECL_PARAMS_RE.finditer(clean):
        params = m.group(1)
        for pm in HEADER_CONST_PARAM_RE.finditer(params):
            offset = m.start(1) + pm.start()
            line_no = clean[:offset].count("\n") + 1
            findings.header_const_params.append(f"{f.rel_path}:{line_no} — {pm.group(0).strip()[:90]}")


def _scan_codebase_findings(files: list[SourceFile]) -> _AuditFindings:
    findings = _AuditFindings([], [], defaultdict(int), [], [], [], [], [])
    for f in files:
        raw_lines = f.text.splitlines()
        clean = strip_comments_preserve_lines(f.text)
        clean_lines = clean.splitlines()
        _scan_clean_lines(f, clean_lines, findings)
        _scan_raw_lines(f, raw_lines, findings)
        if f.rel_path.endswith(".h"):
            _scan_header_declarations(f, clean, clean_lines, findings)
    return findings


def _attach_node_alerts(
    nodes: list[dict[str, Any]],
    fat_bases: list[dict[str, Any]],
    inverted_private: list[dict[str, Any]],
    dynamic_cast_targets: dict[str, int],
    large_files: list[SourceFile],
) -> dict[str, dict[str, Any]]:
    node_by_id = {n["id"]: n for n in nodes}
    for n in fat_bases:
        n["alerts"].append(f"{n['virtual_count']} virtuals")
        n["check_ids"].append("A3")
    for n in inverted_private:
        n["alerts"].append("private: order")
        n["check_ids"].append("A10")
    for target, count in dynamic_cast_targets.items():
        if target in node_by_id:
            node_by_id[target]["alerts"].append(f"{count}x RTTI")
            node_by_id[target]["check_ids"].append("A2")
    large_paths = {f.rel_path for f in large_files}
    for n in nodes:
        if n["file"] in large_paths and n["lines"] >= 200:
            n["alerts"].append(f"{n['lines']}L")
            n["check_ids"].append("A5")
        if "const_cast!" in n["alerts"]:
            n["check_ids"].append("A1")
        n["alerts"] = list(dict.fromkeys(n["alerts"]))
        n["check_ids"] = list(dict.fromkeys(n["check_ids"]))
    return node_by_id


def _make_audit_check(
    node_by_id: dict[str, dict[str, Any]],
    cid: str,
    severity: str,
    category: str,
    title: str,
    summary: str,
    recommendation: str,
    items: list[str],
    related_nodes: list[str] | None = None,
) -> dict[str, Any]:
    return {
        "id": cid,
        "severity": severity,
        "category": category,
        "title": title,
        "summary": summary,
        "recommendation": recommendation,
        "count": len(items),
        "status": "resolved" if len(items) == 0 else "open",
        "items": items[:40],
        "total_items": len(items),
        "nodes": [nid for nid in (related_nodes or []) if nid in node_by_id],
    }


AUDIT_CHECK_METADATA: tuple[tuple[str, str, str, str, str, str], ...] = (
    (
        "A1",
        "crit",
        "Immutability & Thread-Safety",
        "Use of const_cast in domain code",
        "Casting away const on objects passed by const & risks undefined behavior on truly const instances and prevents concurrent runs.",
        "Separate the immutable compiled model from mutable per-run simulation state and remove const_cast.",
    ),
    (
        "A2",
        "crit",
        "Polymorphism & OCP",
        "dynamic_cast calls outside the device hierarchy",
        "Parallel dynamic_cast chains couple analyses to concrete subclasses and require edits across multiple files whenever a device is added.",
        "Replace with polymorphic contracts (e.g. parameter derivatives), a shared IndependentSourceDevice base, and typed directive collections.",
    ),
    (
        "A3",
        "high",
        "Interface Segregation (ISP)",
        "Fat base classes (>= 10 virtual methods)",
        "Umbrella interfaces mix distinct roles (DC, AC, noise, HB, transient, environment) and force empty no-op overrides.",
        "Move pseudo-devices (.ic/.nodeset/.seed) out of the base class and segregate optional capabilities.",
    ),
    (
        "A4",
        "high",
        "Module Architecture (DAG)",
        "Module dependency cycles",
        "Direct or public-header cycles break strict layering and couple sibling subsystems.",
        "Move shared utilities (such as spice_number) into a lower module below both callers.",
    ),
    (
        "A5",
        "high",
        "Cohesion & Decomposition",
        "Files exceeding the 500-line review signal (CLAUDE.md)",
        "Large files tend to accumulate multiple topics, macromodels, or unrelated responsibilities.",
        "Extract cohesive groups (macromodels, device families, element dispatchers) into dedicated modules.",
    ),
    (
        "A6",
        "med",
        "Public API Design",
        "Silently ignored parameters ((void)param;) in the API layer",
        "Accepting public parameters and discarding them with (void) surprises callers and hides unsupported model features.",
        "Implement the physical effect, reject unsupported values explicitly, or remove the parameter from the signature.",
    ),
    (
        "A7",
        "med",
        "Function Complexity",
        "Functions in .cpp files exceeding the 40-line signal (CLAUDE.md)",
        "Long functions often mix levels of abstraction; review whether named phases or dispatch tables apply.",
        "Extract named domain steps so each function operates at one level of abstraction.",
    ),
    (
        "A8",
        "low",
        "Return Safety (CLAUDE.md)",
        "Header declarations returning std::expected / std::optional without [[nodiscard]]",
        "Omitting [[nodiscard]] allows callers to silently discard errors or missing results.",
        "Add [[nodiscard]] to public and internal header declarations.",
    ),
    (
        "A9",
        "low",
        "C++ Idioms (CLAUDE.md)",
        "By-value parameters marked const in header declarations",
        "CLAUDE.md marks by-value parameters const only in definitions (.cpp), never in declarations (.h).",
        "Remove const from by-value parameters in headers (enforced by readability-avoid-const-params-in-decls).",
    ),
    (
        "A10",
        "low",
        "Class Layout (CLAUDE.md)",
        "private: sections placing methods before data members",
        "The guideline orders private: as nested types -> members -> private methods.",
        "Move private data members above private methods in the listed classes.",
    ),
    (
        "A11",
        "low",
        "Doxygen Style (CLAUDE.md)",
        "Trailing ///< comments instead of /// above declarations",
        "The guideline places /// comments above each individual declaration instead of trailing ///< comments.",
        "Convert ///< comments to /// above each declaration (tracked alongside #275).",
    ),
    (
        "A12",
        "low",
        "Codebase Hygiene",
        "TODO/FIXME, #pragma once, using namespace, raw new/delete, or throw",
        "Baseline hygiene invariants from the global guidelines.",
        "Keep at zero.",
    ),
)


def _build_audit_checks(
    findings: _AuditFindings,
    large_files: list[SourceFile],
    long_funcs: list[dict[str, Any]],
    fat_bases: list[dict[str, Any]],
    inverted_private: list[dict[str, Any]],
    cycles: list[list[str]],
    nodes: list[dict[str, Any]],
    node_by_id: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    large_paths = {f.rel_path for f in large_files}
    large_node_ids = [n["id"] for n in nodes if n["file"] in large_paths and n["lines"] >= 200][:6]
    payloads: dict[str, tuple[list[str], list[str]]] = {
        "A1": (findings.const_casts, ["Circuit", "CircuitImpl", "CompiledCircuit"]),
        "A2": (findings.dynamic_casts, list(findings.dynamic_cast_targets.keys())[:6]),
        "A3": (
            [f"{n['id']} ({n['file']}:{n['line']}) — {n['virtual_count']} virtual methods" for n in fat_bases],
            [n["id"] for n in fat_bases],
        ),
        "A4": ([" → ".join(cyc) for cyc in cycles], []),
        "A5": ([f"{f.rel_path} — {f.lines} lines" for f in large_files], large_node_ids),
        "A6": (findings.ignored_params, ["CircuitBuilder", "CircuitBuilderImpl"]),
        "A7": ([f"{fn['file']}:{fn['line']} ({fn['length']}L) — {fn['signature']}" for fn in long_funcs], []),
        "A8": (findings.missing_nodiscard, ["CircuitBuilder", "Expression"]),
        "A9": (findings.header_const_params, []),
        "A10": (
            [f"{n['id']} ({n['file']}:{n['line']})" for n in inverted_private],
            [n["id"] for n in inverted_private],
        ),
        "A11": (findings.trailing_doxygen, []),
        "A12": (findings.hygiene_issues, []),
    }
    return [_make_audit_check(node_by_id, *meta, *payloads[meta[0]]) for meta in AUDIT_CHECK_METADATA]


def _build_audit_kpis(
    files: list[SourceFile],
    nodes: list[dict[str, Any]],
    findings: _AuditFindings,
    large_files: list[SourceFile],
    long_funcs: list[dict[str, Any]],
    cycles: list[list[str]],
    checks: list[dict[str, Any]],
) -> dict[str, int]:
    return {
        "files": len(files),
        "loc": sum(f.lines for f in files),
        "classes": sum(1 for n in nodes if n["kind"] == "class"),
        "structs": sum(1 for n in nodes if n["kind"] == "struct"),
        "const_cast": len(findings.const_casts),
        "dynamic_cast": len(findings.dynamic_casts),
        "cycles": len(cycles),
        "large_files": len(large_files),
        "long_funcs": len(long_funcs),
        "missing_nodiscard": len(findings.missing_nodiscard),
        "hygiene_issues": len(findings.hygiene_issues),
        "open_checks": sum(1 for c in checks if c["status"] == "open"),
        "resolved_checks": sum(1 for c in checks if c["status"] == "resolved"),
    }


def run_live_audit(
    files: list[SourceFile],
    nodes: list[dict[str, Any]],
    module_graph: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """Computes live architectural and guideline compliance checks directly from the codebase."""
    findings = _scan_codebase_findings(files)
    large_files = sorted([f for f in files if f.lines > 500], key=lambda item: -item.lines)
    long_funcs = scan_long_functions(files, threshold=40)
    fat_bases = [n for n in nodes if n["virtual_count"] >= 10]
    inverted_private = [n for n in nodes if n["private_order_inverted"]]
    cycles = module_graph["cycles"]

    node_by_id = _attach_node_alerts(nodes, fat_bases, inverted_private, findings.dynamic_cast_targets, large_files)
    checks = _build_audit_checks(
        findings, large_files, long_funcs, fat_bases, inverted_private, cycles, nodes, node_by_id
    )
    kpis = _build_audit_kpis(files, nodes, findings, large_files, long_funcs, cycles, checks)
    return checks, kpis


def collect_target_files(root_dir: Path, target_cfg: dict[str, Any]) -> list[SourceFile]:
    """Collects all C++ headers and sources belonging to a configured target."""
    files: list[SourceFile] = []
    for rel_root, forced_module in target_cfg["roots"]:
        base = root_dir / rel_root
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            rel = path.relative_to(root_dir).as_posix()
            if any(seg in f"/{rel}" for seg in EXCLUDED_SEGMENTS):
                continue
            if forced_module:
                module = forced_module
            else:
                sub = path.relative_to(base).parts
                module = sub[0] if len(sub) > 1 else "root"
            text = path.read_text(encoding="utf-8", errors="replace")
            files.append(SourceFile(rel_path=rel, module=module, text=text, lines=len(text.splitlines())))
    return files


def build_payload(target_name: str, root_dir: Path = PROJECT_ROOT) -> dict[str, Any]:
    """Extracts the complete JSON payload for a target library."""
    if target_name not in TARGETS:
        raise KeyError(f"Unknown target '{target_name}'. Available: {', '.join(sorted(TARGETS))}")
    cfg = TARGETS[target_name]
    files = collect_target_files(root_dir, cfg)

    all_nodes: list[dict[str, Any]] = []
    seen_ids: dict[str, int] = {}
    for f in files:
        for node in extract_types_from_file(f):
            # Disambiguate rare internal classes sharing a generic name across .cpp files (e.g. Parser).
            base_id = node["id"]
            if base_id in seen_ids:
                seen_ids[base_id] += 1
                node["id"] = f"{base_id} ({node['module']})"
            else:
                seen_ids[base_id] = 1
            all_nodes.append(node)

    module_graph = build_module_graph(files, cfg)
    edges = infer_relationships(all_nodes, files)
    checks, kpis = run_live_audit(files, all_nodes, module_graph)

    mod_meta = {m["id"]: m for m in module_graph["modules"]}
    return {
        "target": target_name,
        "title": cfg["title"],
        "subtitle": cfg["subtitle"],
        "kpis": kpis,
        "modules": mod_meta,
        "module_order": [m["id"] for m in module_graph["modules"]],
        "nodes": all_nodes,
        "edges": edges,
        "module_graph": module_graph,
        "checks": checks,
    }


def render_html(payload: dict[str, Any], template_path: Path = TEMPLATE_PATH) -> str:
    """Injects the JSON payload into the standalone HTML template."""
    template = template_path.read_text(encoding="utf-8")
    # Escape </ so no string inside JSON can close the <script> tag prematurely.
    serialized = json.dumps(payload, ensure_ascii=False, indent=None).replace("</", "<\\/")
    html = template.replace("__TITLE__", payload["title"])
    html = html.replace("__SUBTITLE__", payload["subtitle"])
    html = html.replace("__PAYLOAD__", serialized)
    return html


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate an interactive C++ class diagram and live audit HTML.")
    parser.add_argument("target", choices=sorted(TARGETS.keys()), help="Configured library target to analyze.")
    parser.add_argument("--output", type=Path, help="Override the default output HTML path.")
    parser.add_argument("--check", action="store_true", help="Verify the generated HTML is up to date without writing.")
    parser.add_argument("--json-only", action="store_true", help="Print extracted JSON payload to stdout.")
    args = parser.parse_args(argv)

    payload = build_payload(args.target)
    if args.json_only:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0

    out_path = args.output or (PROJECT_ROOT / TARGETS[args.target]["output"])
    html = render_html(payload)

    if args.check:
        if not out_path.exists():
            print(f"Missing diagram: {out_path}", file=sys.stderr)
            return 1
        existing = out_path.read_text(encoding="utf-8")
        if existing != html:
            print(f"Out of date: {out_path}. Regenerate with: .venv/bin/python tools/class_diagram.py {args.target}", file=sys.stderr)
            return 1
        print(f"Up to date: {out_path}")
        return 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(html, encoding="utf-8")
    k = payload["kpis"]
    print(
        f"Generated {out_path} ({k['files']} files, {k['loc']} LOC, "
        f"{k['classes']} classes, {k['structs']} structs, {len(payload['edges'])} edges, "
        f"{k['open_checks']} open checks / {k['resolved_checks']} resolved)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

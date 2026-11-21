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
        "title": "Nodalys — Diagrama de Classes & Auditoria Viva",
        "subtitle": "Motor de simulação C++23 (nodalys/) + ponte imcsim (app-core/backends/nodalys/)",
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


def strip_comments_preserve_lines(code: str) -> str:
    """Removes C/C++ comments and string literals while preserving line numbers."""
    out: list[str] = []
    i = 0
    n = len(code)
    while i < n:
        ch = code[i]
        nxt = code[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and code[i] != "\n":
                i += 1
        elif ch == "/" and nxt == "*":
            i += 2
            while i < n:
                if code[i] == "\n":
                    out.append("\n")
                if code[i] == "*" and i + 1 < n and code[i + 1] == "/":
                    i += 2
                    break
                i += 1
        elif ch in ('"', "'"):
            quote = ch
            out.append(quote)
            i += 1
            while i < n:
                c = code[i]
                if c == "\\":
                    i += 2
                    continue
                if c == "\n":
                    out.append("\n")
                    i += 1
                    break
                if c == quote:
                    out.append(quote)
                    i += 1
                    break
                i += 1
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


def extract_preceding_brief(raw_text: str, start_line: int) -> str:
    """Extracts the Doxygen @brief or /// comment immediately above start_line (1-based)."""
    lines = raw_text.splitlines()[: max(0, start_line - 1)]
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines:
        return ""

    if lines[-1].strip().endswith("*/"):
        block_lines: list[str] = []
        for line in reversed(lines):
            block_lines.append(line)
            if "/**" in line or "/*" in line:
                break
        block = "\n".join(reversed(block_lines))
        if "/**" in block:
            brief_match = re.search(r"@brief\s+(.*?)(?=\n\s*\*\s*@|\*/)", block, re.DOTALL)
            if brief_match:
                cleaned = re.sub(r"\n\s*\*\s*", " ", brief_match.group(1)).strip()
                return re.sub(r"\s+", " ", cleaned)

    slash_lines: list[str] = []
    for line in reversed(lines):
        stripped = line.strip()
        if stripped.startswith("///"):
            slash_lines.append(stripped.lstrip("/").strip())
        else:
            break
    if slash_lines:
        return " ".join(reversed(slash_lines))
    return ""


def parse_member_sections(body: str, default_visibility: str) -> tuple[list[str], list[str], dict[str, Any]]:
    """Splits a class/struct body into top-level fields and methods with visibility markers."""
    vis_symbol = {"public": "+", "protected": "#", "private": "-"}
    visibility = default_visibility
    fields: list[str] = []
    methods: list[str] = []
    virtual_count = 0
    pure_virtual_count = 0
    private_events: list[str] = []

    # Collect top-level statements inside the class body (depth == 0).
    statements: list[tuple[str, str]] = []
    buf: list[str] = []
    depth = 0
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c == "{":
            depth += 1
            buf.append(c)
        elif c == "}":
            depth = max(0, depth - 1)
            buf.append(c)
            if depth == 0:
                stmt = "".join(buf).strip()
                if stmt:
                    statements.append((visibility, stmt))
                buf = []
        elif depth == 0 and c == ";":
            stmt = "".join(buf).strip()
            if stmt:
                statements.append((visibility, stmt))
            buf = []
        elif depth == 0 and c == ":" and (i + 1 >= n or body[i + 1] != ":"):
            candidate = "".join(buf).strip()
            if candidate in ("public", "protected", "private"):
                visibility = candidate
                buf = []
            else:
                buf.append(c)
        else:
            buf.append(c)
        i += 1

    for vis, raw_stmt in statements:
        stmt = re.sub(r"\s+", " ", raw_stmt).strip()
        if not stmt or stmt.startswith("friend ") or stmt.startswith("static_assert"):
            continue
        if re.match(r"^(?:template\s*<[^>]+>\s*)?(class|struct|enum)\b", stmt):
            if vis == "private":
                private_events.append("type")
            continue
        if stmt.startswith("using "):
            if vis == "private":
                private_events.append("type")
            continue

        # Strip inline function body for readability.
        header_part = stmt.split("{", 1)[0].strip()
        is_method = "(" in header_part and not re.match(r"^[A-Za-z0-9_:<>,\s*&]+\s+[A-Za-z_][A-Za-z0-9_]*\s*=", header_part)
        # Disambiguate std::function<void(...)> member fields.
        if "std::function<" in header_part:
            after_fn = header_part.split("std::function<", 1)[1]
            # If closing '>' precedes any parameter list, it is a field unless followed by '('.
            angle_depth = 1
            pos = 0
            while pos < len(after_fn) and angle_depth > 0:
                if after_fn[pos] == "<":
                    angle_depth += 1
                elif after_fn[pos] == ">":
                    angle_depth -= 1
                pos += 1
            tail = after_fn[pos:]
            is_method = "(" in tail

        sym = vis_symbol[vis]
        if is_method:
            if re.search(r"\bvirtual\b", header_part):
                virtual_count += 1
            if re.search(r"=\s*0\s*$", header_part):
                pure_virtual_count += 1
            methods.append(f"{sym} {header_part}")
            if vis == "private":
                private_events.append("method")
        else:
            clean_field = re.sub(r"\s*=\s*.*$", "", header_part).strip()
            if clean_field:
                fields.append(f"{sym} {clean_field}")
                if vis == "private":
                    private_events.append("field")

    # CLAUDE.md rule: in private:, nested types first, then members, then private methods.
    first_priv_method = next((idx for idx, kind in enumerate(private_events) if kind == "method"), None)
    last_priv_field = next((idx for idx in range(len(private_events) - 1, -1, -1) if private_events[idx] == "field"), None)
    private_order_inverted = (
        first_priv_method is not None and last_priv_field is not None and first_priv_method < last_priv_field
    )

    stats = {
        "virtual_count": virtual_count,
        "pure_virtual_count": pure_virtual_count,
        "private_order_inverted": private_order_inverted,
    }
    return fields, methods, stats


def extract_types_from_file(src: SourceFile) -> list[dict[str, Any]]:
    """Extracts top-level C++ classes and structs from a single source file."""
    clean = strip_comments_preserve_lines(src.text)
    results: list[dict[str, Any]] = []

    # Track byte ranges of already-captured outer types to skip nested structs/classes.
    captured_ranges: list[tuple[int, int]] = []
    for m in TYPE_DECL_RE.finditer(clean):
        if any(start < m.start() < end for start, end in captured_ranges):
            continue
        brace_pos = m.end() - 1
        end_pos = find_matching_brace(clean, brace_pos)
        if end_pos == brace_pos:
            continue
        captured_ranges.append((m.start(), end_pos))

        tmpl = (m.group(1) or "").strip()
        kind = m.group(2).strip()
        name = m.group(3).strip()
        bases_raw = (m.group(4) or "").strip()
        bases: list[str] = []
        if bases_raw:
            for base in bases_raw.split(","):
                cleaned = re.sub(r"\b(public|protected|private|virtual)\b", "", base).strip()
                cleaned = cleaned.split("<", 1)[0].split("::")[-1].strip()
                if cleaned:
                    bases.append(cleaned)

        start_line = clean[: m.start()].count("\n") + 1
        end_line = clean[:end_pos].count("\n") + 1
        body = clean[m.end() : end_pos]
        default_vis = "private" if kind == "class" else "public"
        fields, methods, stats = parse_member_sections(body, default_vis)
        brief = extract_preceding_brief(src.text, start_line)

        non_eq_methods = [mt for mt in methods if "operator==" not in mt and "operator<=>" not in mt]
        is_pod = kind == "struct" and len(non_eq_methods) == 0 and len(bases) == 0

        if stats["pure_virtual_count"] > 0:
            stereotype = f"«abstract {kind}»"
        elif tmpl:
            stereotype = f"«template {kind}»"
        elif src.rel_path.endswith(".cpp"):
            stereotype = f"«internal {kind}»"
        elif is_pod:
            stereotype = "«data struct»"
        else:
            stereotype = f"«{kind}»"

        results.append(
            {
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
                "summary": brief or f"Definido em {src.rel_path}:{start_line}.",
                "fields": fields,
                "methods": methods,
                "is_pod": is_pod,
                "virtual_count": stats["virtual_count"],
                "pure_virtual_count": stats["pure_virtual_count"],
                "private_order_inverted": stats["private_order_inverted"],
                "alerts": [],
                "check_ids": [],
            }
        )
    return results


def infer_relationships(nodes: list[dict[str, Any]], files: list[SourceFile]) -> list[dict[str, Any]]:
    """Infers inheritance, composition, aggregation, usage, and smell edges between extracted nodes."""
    node_ids = {n["id"] for n in nodes}
    seen: set[tuple[str, str, str]] = set()
    edges: list[dict[str, Any]] = []

    def add_edge(src_id: str, dst_id: str, rel_type: str, label: str = "") -> None:
        if src_id not in node_ids or dst_id not in node_ids:
            return
        if src_id == dst_id and rel_type != "agg":
            return
        key = (src_id, dst_id, rel_type)
        if key in seen:
            return
        seen.add(key)
        edges.append({"from": src_id, "to": dst_id, "type": rel_type, "label": label})

    # Pre-build regex to find references to known type names in fields/methods.
    sorted_ids = sorted(node_ids, key=len, reverse=True)
    if not sorted_ids:
        return edges
    ident_re = re.compile(r"\b(" + "|".join(re.escape(i) for i in sorted_ids) + r")\b")
    nested_ref_re = re.compile(r"\b(" + "|".join(re.escape(i) for i in sorted_ids) + r")::([A-Za-z_][A-Za-z0-9_]*)")

    strong_links: set[tuple[str, str]] = set()

    for node in nodes:
        src_id = node["id"]
        for base in node["bases"]:
            if base in node_ids:
                add_edge(src_id, base, "inherit")
                strong_links.add((src_id, base))

        for field in node["fields"]:
            decl = field[2:]
            # Detect sibling nested type coupling like VoltageSourceDevice::Waveform
            for nm in nested_ref_re.finditer(decl):
                target = nm.group(1)
                if target != src_id and target not in node["bases"]:
                    add_edge(src_id, target, "smell", f"usa ::{nm.group(2)}")

            for tm in ident_re.finditer(decl):
                target = tm.group(1)
                if target == src_id and ("*" in decl or "&" in decl):
                    add_edge(src_id, target, "agg", "self-ref")
                    strong_links.add((src_id, target))
                    continue
                if target == src_id:
                    continue
                if re.search(rf"std::(?:unique|shared)_ptr\s*<\s*(?:const\s+)?{re.escape(target)}\b", decl):
                    add_edge(src_id, target, "compose", "smart_ptr")
                    strong_links.add((src_id, target))
                elif re.search(rf"\b{re.escape(target)}\s*[*&]", decl) or re.search(
                    rf"std::(?:vector|map|span)\s*<[^>]*\b{re.escape(target)}\s*\*", decl
                ):
                    add_edge(src_id, target, "agg")
                    strong_links.add((src_id, target))
                else:
                    add_edge(src_id, target, "compose")
                    strong_links.add((src_id, target))

        for method in node["methods"]:
            decl = method[2:]
            for tm in ident_re.finditer(decl):
                target = tm.group(1)
                if target != src_id and (src_id, target) not in strong_links:
                    add_edge(src_id, target, "uses")

    # Add smell edges for const_cast targeting a known class.
    const_cast_re = re.compile(r"const_cast\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*[*&]")
    for f in files:
        clean = strip_comments_preserve_lines(f.text)
        for m in const_cast_re.finditer(clean):
            target = m.group(1)
            if target in node_ids:
                # Attach smell to any node in this file or mark on target node directly.
                for n in nodes:
                    if n["id"] == target:
                        n["alerts"].append("const_cast!")

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

    # Direct public-api -> owner edges for visualization of indirect cycles.
    for hdr, owner in public_owners.items():
        if "public-api" in known_modules and owner in known_modules:
            effective_graph["public-api"].add(owner)

    # Compute cycles on internal modules (ignoring public-api hub unless part of an indirect cycle).
    internal_graph: dict[str, set[str]] = defaultdict(set)
    for src_mod, dsts in effective_graph.items():
        if src_mod == "public-api":
            continue
        for dst in dsts:
            if dst != "public-api" and dst != src_mod:
                internal_graph[src_mod].add(dst)

    cycles = find_cycles(internal_graph)
    cycle_pairs: set[tuple[str, str]] = set()
    for cyc in cycles:
        for a, b in zip(cyc, cyc[1:]):
            cycle_pairs.add((a, b))

    mod_order = target_cfg.get("module_order", [])
    ordered_mods = [m for m in mod_order if m in known_modules] + sorted(known_modules - set(mod_order))
    colors = target_cfg.get("module_colors", {})

    modules_list = []
    for idx, mod in enumerate(ordered_mods):
        modules_list.append(
            {
                "id": mod,
                "files": files_by_mod[mod],
                "loc": loc_by_mod[mod],
                "color": colors.get(mod, PALETTE[idx % len(PALETTE)]),
            }
        )

    edges_list = []
    for (src_mod, dst_mod), count in sorted(direct_counts.items()):
        is_cycle = (src_mod, dst_mod) in cycle_pairs
        edges_list.append({"from": src_mod, "to": dst_mod, "count": count, "cycle": is_cycle})

    # Add explicit cycle edges that flow indirectly through public-api so they render clearly.
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

    return {
        "modules": modules_list,
        "edges": edges_list,
        "cycles": cycles,
        "cycle_examples": {f"{a} -> {b}": vals for (a, b), vals in sorted(effective_examples.items()) if (a, b) in cycle_pairs},
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


def run_live_audit(
    files: list[SourceFile],
    nodes: list[dict[str, Any]],
    module_graph: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """Computes live architectural and guideline compliance checks directly from the codebase."""
    const_casts: list[str] = []
    dynamic_casts: list[str] = []
    dynamic_cast_targets: dict[str, int] = defaultdict(int)
    missing_nodiscard: list[str] = []
    header_const_params: list[str] = []
    trailing_doxygen: list[str] = []
    ignored_params: list[str] = []
    hygiene_issues: list[str] = []

    dyn_re = re.compile(r"dynamic_cast\s*<\s*(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*[*&]")
    nodiscard_re = re.compile(
        r"^[ \t]*(?:virtual\s+|static\s+)?(std::(?:expected|optional)<[^>]+>)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
        re.MULTILINE,
    )
    void_param_re = re.compile(r"^\s*\(void\)\s*([a-z_][a-z0-9_]*)\s*;\s*$")

    for f in files:
        raw_lines = f.text.splitlines()
        clean = strip_comments_preserve_lines(f.text)
        clean_lines = clean.splitlines()

        for idx, line in enumerate(clean_lines, 1):
            if "const_cast" in line:
                const_casts.append(f"{f.rel_path}:{idx} — {line.strip()[:90]}")
            for dm in dyn_re.finditer(line):
                dynamic_casts.append(f"{f.rel_path}:{idx} — {line.strip()[:90]}")
                dynamic_cast_targets[dm.group(1)] += 1
            if re.search(r"^\s*#pragma\s+once\b", line):
                hygiene_issues.append(f"#pragma once em {f.rel_path}:{idx}")
            if "using namespace " in line:
                hygiene_issues.append(f"using namespace em {f.rel_path}:{idx}")
            if re.search(r"\b(new |delete |malloc\(|free\()", line):
                hygiene_issues.append(f"alocação crua em {f.rel_path}:{idx}")
            if re.search(r"\bthrow\b", line):
                hygiene_issues.append(f"throw em {f.rel_path}:{idx}")
            if f.rel_path.endswith(".cpp") and f.module == "api":
                vm = void_param_re.match(line)
                if vm:
                    ignored_params.append(f"{f.rel_path}:{idx} — (void){vm.group(1)};")

        for idx, raw_line in enumerate(raw_lines, 1):
            if "///<" in raw_line:
                trailing_doxygen.append(f"{f.rel_path}:{idx} — {raw_line.strip()[:90]}")
            if re.search(r"\b(TODO|FIXME|HACK|XXX)\b", raw_line):
                hygiene_issues.append(f"TODO/FIXME em {f.rel_path}:{idx}")

        if f.rel_path.endswith(".h"):
            for m in nodiscard_re.finditer(clean):
                line_start = clean.rfind("\n", 0, m.start()) + 1
                prev_start = clean.rfind("\n", 0, max(0, line_start - 1)) + 1
                if "[[nodiscard]]" not in clean[prev_start : m.start()]:
                    line_no = clean[: m.start()].count("\n") + 1
                    missing_nodiscard.append(f"{f.rel_path}:{line_no} — {m.group(1)} {m.group(2)}()")
            for idx, line in enumerate(clean_lines, 1):
                if re.search(r"\bconst\s+(?:int|double|bool|std::size_t|float)\s+[a-z_]", line) and "{" not in line:
                    header_const_params.append(f"{f.rel_path}:{idx} — {line.strip()[:90]}")

    large_files = sorted([f for f in files if f.lines > 500], key=lambda item: -item.lines)
    long_funcs = scan_long_functions(files, threshold=40)
    fat_bases = [n for n in nodes if n["virtual_count"] >= 10]
    inverted_private = [n for n in nodes if n["private_order_inverted"]]
    cycles = module_graph["cycles"]

    # Attach live alerts to affected nodes.
    node_by_id = {n["id"]: n for n in nodes}
    for n in fat_bases:
        n["alerts"].append(f"{n['virtual_count']} virtuals")
        n["check_ids"].append("A3")
    for n in inverted_private:
        n["alerts"].append("private: ordem")
        n["check_ids"].append("A10")
    for target, count in dynamic_cast_targets.items():
        if target in node_by_id:
            node_by_id[target]["alerts"].append(f"{count}x RTTI")
            node_by_id[target]["check_ids"].append("A2")
    for f in large_files:
        for n in nodes:
            if n["file"] == f.rel_path and n["lines"] >= 200:
                n["alerts"].append(f"{n['lines']}L")
                n["check_ids"].append("A5")
    for n in nodes:
        if "const_cast!" in n["alerts"]:
            n["check_ids"].append("A1")
        n["alerts"] = list(dict.fromkeys(n["alerts"]))
        n["check_ids"] = list(dict.fromkeys(n["check_ids"]))

    def make_check(
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

    checks = [
        make_check(
            "A1",
            "crit",
            "Imutabilidade & Thread-Safety",
            "Uso de const_cast em código de domínio",
            "Remover const de objetos recebidos como const & gera UB potencial em objetos verdadeiramente const e inviabiliza execuções concorrentes.",
            "Separar o modelo compilado imutável do estado/workspace mutável de simulação e remover const_cast.",
            const_casts,
            ["Circuit", "CircuitImpl", "CompiledCircuit"],
        ),
        make_check(
            "A2",
            "crit",
            "Polimorfismo & OCP",
            "Chamadas de dynamic_cast fora da hierarquia de dispositivos",
            "Cadeias paralelas de dynamic_cast acoplam análises a subclasses concretas e exigem alterações em vários arquivos a cada novo dispositivo.",
            "Substituir por contratos polimórficos (ex.: derivadas paramétricas), base comum IndependentSourceDevice e coleções tipadas para diretivas.",
            dynamic_casts,
            list(dynamic_cast_targets.keys())[:6],
        ),
        make_check(
            "A3",
            "high",
            "Interface Segregation (ISP)",
            "Classes base 'gordas' (≥ 10 métodos virtuais)",
            "Interfaces guarda-chuva misturam papéis distintos (DC, AC, ruído, HB, transiente, ambiente) e forçam overrides vazios.",
            "Retirar pseudo-devices (.ic/.nodeset/.seed) da base e segregar capacidades opcionais.",
            [f"{n['id']} ({n['file']}:{n['line']}) — {n['virtual_count']} métodos virtuais" for n in fat_bases],
            [n["id"] for n in fat_bases],
        ),
        make_check(
            "A4",
            "high",
            "Arquitetura de Módulos (DAG)",
            "Ciclos de dependência entre módulos",
            "Ciclos diretos ou via header público impedem camadas independentes e acoplam subsistemas irmãos.",
            "Mover utilitários compartilhados (ex.: spice_number) para uma camada base comum abaixo de ambos os módulos.",
            [" → ".join(cyc) for cyc in cycles],
            [],
        ),
        make_check(
            "A5",
            "high",
            "Coesão & Decomposição",
            "Arquivos acima do sinal de 500 linhas (CLAUDE.md)",
            "Arquivos muito extensos tendem a misturar múltiplos tópicos ou responsabilidades de compilação/macromodelos.",
            "Extrair grupos coesos (macromodelos, famílias de dispositivos, dispatchers) em módulos dedicados.",
            [f"{f.rel_path} — {f.lines} linhas" for f in large_files],
            [n["id"] for n in nodes if n["file"] in {f.rel_path for f in large_files} and n["lines"] >= 200][:6],
        ),
        make_check(
            "A6",
            "med",
            "Design de API Pública",
            "Parâmetros silenciosamente ignorados ((void)param;) na camada API",
            "Aceitar parâmetros na API pública e descartá-los com (void) surpreende o chamador e mascara limitações do modelo.",
            "Implementar o efeito físico, recusar valores não suportados explicitamente ou remover o parâmetro da assinatura.",
            ignored_params,
            ["CircuitBuilder", "CircuitBuilderImpl"],
        ),
        make_check(
            "A7",
            "med",
            "Complexidade de Funções",
            "Funções em .cpp acima do sinal de 40 linhas (CLAUDE.md)",
            "Funções extensas costumam misturar níveis de abstração; revisar se há fases nomeáveis ou tabelas de despacho.",
            "Extrair fases nomeadas mantendo uma responsabilidade por nível de abstração.",
            [f"{fn['file']}:{fn['line']} ({fn['length']}L) — {fn['signature']}" for fn in long_funcs],
            [],
        ),
        make_check(
            "A8",
            "low",
            "Segurança de Retorno (CLAUDE.md)",
            "Declarações retornando std::expected / std::optional sem [[nodiscard]]",
            "Omitir [[nodiscard]] permite descartar erros ou resultados ausentes sem aviso do compilador.",
            "Adicionar [[nodiscard]] às declarações em headers públicos e internos.",
            missing_nodiscard,
            ["CircuitBuilder", "Expression"],
        ),
        make_check(
            "A9",
            "low",
            "Idiomas C++ (CLAUDE.md)",
            "Parâmetros por valor marcados const em headers",
            "O CLAUDE.md define const em parâmetros por valor apenas na definição (.cpp), nunca na declaração (.h).",
            "Remover const de parâmetros por valor nas declarações de header (enforçado por readability-avoid-const-params-in-decls).",
            header_const_params,
            [],
        ),
        make_check(
            "A10",
            "low",
            "Layout de Classe (CLAUDE.md)",
            "Seção private: com métodos antes de membros",
            "A convenção do guia ordena private: como nested types → members → private methods.",
            "Reordenar os membros privados antes dos métodos privados nas classes listadas.",
            [f"{n['id']} ({n['file']}:{n['line']})" for n in inverted_private],
            [n["id"] for n in inverted_private],
        ),
        make_check(
            "A11",
            "low",
            "Documentação Doxygen (CLAUDE.md)",
            "Comentários trailing ///< em vez de /// acima da declaração",
            "O guia padroniza comentários /// acima de cada declaração individual, evitando comentários trailing ///<.",
            "Converter comentários ///< para /// acima da linha correspondente (alinhado à task #275).",
            trailing_doxygen,
            [],
        ),
        make_check(
            "A12",
            "low",
            "Higiene de Base",
            "TODO/FIXME, #pragma once, using namespace, new/delete crus ou throw",
            "Verificação de invariantes básicos de higiene do guia global.",
            "Manter sempre em zero.",
            hygiene_issues,
            [],
        ),
    ]

    kpis = {
        "files": len(files),
        "loc": sum(f.lines for f in files),
        "classes": sum(1 for n in nodes if n["kind"] == "class"),
        "structs": sum(1 for n in nodes if n["kind"] == "struct"),
        "const_cast": len(const_casts),
        "dynamic_cast": len(dynamic_casts),
        "cycles": len(cycles),
        "large_files": len(large_files),
        "long_funcs": len(long_funcs),
        "missing_nodiscard": len(missing_nodiscard),
        "hygiene_issues": len(hygiene_issues),
        "open_checks": sum(1 for c in checks if c["status"] == "open"),
        "resolved_checks": sum(1 for c in checks if c["status"] == "resolved"),
    }
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

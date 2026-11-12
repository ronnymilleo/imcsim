#!/usr/bin/env python3
"""Draws the include dependencies between the folders of imcsim and refuses includes that go against the layers.

Every tracked .h and .cpp of app-core and app-gui is read for its #include lines. Each include is resolved to the
topic folder of the library it lands in (a file at the library root is the "root" node), which gives the folder graph
with edge counts. The graph is written as Mermaid text to docs/architecture.md.

The declared layers below say which folders may include which: a folder may include its own layer and the layers
below it. An include that goes up is a violation. Violations that exist today are listed in
include_layers_baseline.json; the check fails on a violation that is not listed and on a listed one that is gone, so
the list can only shrink.

Usage:
    python tools/include_layers.py                   write docs/architecture.md, then check the layers
    python tools/include_layers.py --check           write nothing; fail when the diagram is out of date too
    python tools/include_layers.py --update-baseline drop the listed violations that are gone (never adds one)
"""

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BASELINE_PATH = Path(__file__).resolve().parent / "include_layers_baseline.json"
DIAGRAM_PATH = PROJECT_ROOT / "docs" / "architecture.md"

CORE = "app-core"
GUI = "app-gui"
NODALYS = "nodalys"
ROOT_FOLDER = "root"

# Lowest first. A library may include the libraries before it in this list, never the ones after it
LIBRARY_LAYERS = [[NODALYS], [CORE], [GUI]]

# Lowest first, one list per layer. Folders in the same layer may include each other. These are the folders as they
# are today, ordered as close as possible to the target of jay #211: model and components are one layer because they
# are merged there, and the ngspice and nodalys backends sit on top of the simulation that they plug into. Update
# this list in the same change that moves a folder
FOLDER_LAYERS = {
    CORE: [
        ["spice"],
        ["model", "components"],
        ["simulation"],
        ["ngspice", "nodalys"],
    ],
    GUI: [
        ["ui_elements"],
        ["editing", "document"],
        ["analysis"],
        ["plots"],
        ["drawing"],
        ["windows"],
        [ROOT_FOLDER],
    ],
}

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)')
SOURCE_SUFFIXES = (".h", ".cpp")
# Tests and vendored code are not part of the architecture; example.cpp is an untracked prototype
EXCLUDED_PREFIXES = ("vendor/", "tests/", "nodalys/tests/")

DIAGRAM_EXPLANATION = """\
# Architecture

The include dependencies between the folders of imcsim, drawn from the `#include` lines of the sources by
`tools/include_layers.py`. An arrow from A to B means that files of A include files of B, and its number counts
those includes. The folders of each library are listed from the lowest layer to the highest, and a folder may only
include its own layer and the ones below it. A dashed red arrow goes the other way: it is a known violation, listed
in `tools/include_layers_baseline.json`, which `tools/include_layers.py --check` lets only shrink. Tracked
`app-core` and `app-gui` sources are read; `vendor/` and `tests/` are not.

Regenerate this file after a change that adds, removes or moves an include, with the project virtual environment
(the script uses only the standard library):

```
.venv/bin/python tools/include_layers.py
```

The CI runs `python tools/include_layers.py --check`, which fails when this file is out of date or when an include
goes up a layer without being in the baseline.
"""


@dataclass(frozen=True)
class Include:
    """One resolved include between two project files."""

    source_file: str
    line: int
    included_path: str
    source_library: str
    source_folder: str
    target_library: str
    target_folder: str

    @property
    def source_node(self) -> str:
        return node_name(self.source_library, self.source_folder)

    @property
    def target_node(self) -> str:
        return node_name(self.target_library, self.target_folder)

    @property
    def key(self) -> str:
        return f"{self.source_file} -> {self.included_path}"


def node_name(library: str, folder: str) -> str:
    return library if library == NODALYS else f"{library}/{folder}"


def parse_includes(text: str) -> list[tuple[int, str, bool]]:
    """Returns (line number, path, is_angle_bracket) for each include in the text."""
    found = []
    for number, line in enumerate(text.splitlines(), start=1):
        match = INCLUDE_PATTERN.match(line)
        if match:
            quoted, angled = match.groups()
            found.append((number, quoted or angled, quoted is None))
    return found


def library_of(path: str) -> str | None:
    """The library a project-relative file belongs to, or None when it is outside the architecture."""
    if path.startswith(EXCLUDED_PREFIXES) or not path.endswith(SOURCE_SUFFIXES):
        return None
    for library in (CORE, GUI, NODALYS):
        if path.startswith(library + "/"):
            return library
    return None


def folder_of(path: str, library: str) -> str:
    """The topic folder of a file inside its library; files at the library root are the root node."""
    if library == NODALYS:
        return NODALYS
    parts = path.split("/")
    return parts[1] if len(parts) > 2 else ROOT_FOLDER


def resolve_include(included_path: str, is_angle: bool, source_library: str, known_files: set[str]) -> str | None:
    """Finds the project file an include lands in, searching the include roots the library has."""
    if source_library == NODALYS:
        roots = ["nodalys/include", "nodalys/src", CORE, GUI]
    elif source_library == CORE:
        roots = [CORE, "nodalys/include", GUI]
    else:
        roots = [GUI, CORE, "nodalys/include"]
    if is_angle:
        # Angle brackets are the Nodalys public API and the system headers; a core header with a similar path
        # (core/nodalys/...) is always included with quotes
        roots = [root for root in roots if root == "nodalys/include"]
    for root in roots:
        candidate = f"{root}/{included_path}"
        if candidate in known_files:
            return candidate
    return None


def build_includes(sources: dict[str, str]) -> list[Include]:
    """Resolves every include of the given sources (project-relative path to text), sorted for stable output."""
    known_files = set(sources)
    includes = []
    for path, text in sources.items():
        source_library = library_of(path)
        if source_library is None:
            continue
        for line, included_path, is_angle in parse_includes(text):
            target = resolve_include(included_path, is_angle, source_library, known_files)
            if target is None:
                continue
            target_library = NODALYS if target.startswith("nodalys/") else target.split("/")[0]
            includes.append(
                Include(
                    source_file=path,
                    line=line,
                    included_path=included_path,
                    source_library=source_library,
                    source_folder=folder_of(path, source_library),
                    target_library=target_library,
                    target_folder=folder_of(target, target_library),
                )
            )
    return sorted(includes, key=lambda item: (item.source_file, item.line, item.included_path))


def count_edges(includes: list[Include]) -> Counter:
    """Counts the includes between two different folders, keyed by (source node, target node)."""
    return Counter(
        (item.source_node, item.target_node) for item in includes if item.source_node != item.target_node
    )


def layer_index(library: str, folder: str) -> int:
    """The position of a folder in its library's layers; raises when the folder has no declared layer."""
    for index, layer in enumerate(FOLDER_LAYERS[library]):
        if folder in layer:
            return index
    raise KeyError(f"folder '{folder}' of {library} has no layer in FOLDER_LAYERS")


def library_layer_index(library: str) -> int:
    for index, layer in enumerate(LIBRARY_LAYERS):
        if library in layer:
            return index
    raise KeyError(library)


def is_upward(item: Include) -> bool:
    """True when the include goes from a lower layer to a higher one, between folders or between libraries."""
    if item.source_library != item.target_library:
        return library_layer_index(item.source_library) < library_layer_index(item.target_library)
    if item.source_library == NODALYS:
        return False
    return layer_index(item.source_library, item.source_folder) < layer_index(item.target_library, item.target_folder)


def find_violations(includes: list[Include]) -> list[Include]:
    return [item for item in includes if is_upward(item)]


def compare_with_baseline(violations: list[Include], baseline: set[str]) -> tuple[list[Include], list[str]]:
    """Returns the violations missing from the baseline and the baseline entries that no longer exist."""
    current = {item.key for item in violations}
    new_violations = [item for item in violations if item.key not in baseline]
    stale_entries = sorted(baseline - current)
    return new_violations, stale_entries


def mermaid_id(node: str) -> str:
    return node.replace("/", "_").replace("-", "_")


def render_diagram(includes: list[Include]) -> str:
    """The text of docs/architecture.md: explanation, library graph, one folder graph per library, layer lists."""
    edges = count_edges(includes)
    upward_edges = {(item.source_node, item.target_node) for item in find_violations(includes)}
    lines = [DIAGRAM_EXPLANATION]

    lines += ["## Libraries", "", "```mermaid", "flowchart TD"]
    library_edges = Counter(
        (item.source_library, item.target_library) for item in includes if item.source_library != item.target_library
    )
    library_upward = {
        (item.source_library, item.target_library) for item in includes if is_upward(item) and item.source_library != item.target_library
    }
    for library in (GUI, CORE, NODALYS):
        lines.append(f"    {mermaid_id(library)}[{library}]")
    lines += render_edges(library_edges, library_upward)
    lines += ["```", ""]

    for library in (CORE, GUI):
        folders = [folder for layer in FOLDER_LAYERS[library] for folder in layer]
        lines += [f"## {library}", "", "```mermaid", "flowchart TD"]
        for folder in folders:
            lines.append(f"    {mermaid_id(node_name(library, folder))}[{folder}]")
        library_folder_edges = Counter(
            {pair: count for pair, count in edges.items() if pair[0].startswith(library + "/") and pair[1].startswith(library + "/")}
        )
        lines += render_edges(library_folder_edges, upward_edges)
        lines += ["```", "", "Layers, lowest first:", ""]
        for index, layer in enumerate(FOLDER_LAYERS[library], start=1):
            lines.append(f"{index}. {', '.join(f'`{folder}`' for folder in layer)}")
        lines.append("")

    return "\n".join(lines)


def render_edges(edges: Counter, upward: set[tuple[str, str]]) -> list[str]:
    """Mermaid edge lines, sorted; edges that go up a layer are dashed and red."""
    lines = []
    upward_indexes = []
    for number, ((source, target), count) in enumerate(sorted(edges.items())):
        arrow = "-.->" if (source, target) in upward else "-->"
        lines.append(f"    {mermaid_id(source)} {arrow}|{count}| {mermaid_id(target)}")
        if arrow == "-.->":
            upward_indexes.append(number)
    for number in upward_indexes:
        lines.append(f"    linkStyle {number} stroke:#d33,stroke-width:2px")
    return lines


def read_tracked_sources() -> dict[str, str]:
    """Reads the tracked sources of the architecture, so untracked files such as example.cpp are left out."""
    listing = subprocess.run(
        ["git", "ls-files", "-z"], cwd=PROJECT_ROOT, check=True, capture_output=True, text=True
    ).stdout
    sources = {}
    for path in listing.split("\0"):
        if library_of(path) is not None:
            sources[path] = (PROJECT_ROOT / path).read_text(encoding="utf-8", errors="replace")
    return sources


def read_baseline() -> set[str]:
    if not BASELINE_PATH.exists():
        return set()
    return set(json.loads(BASELINE_PATH.read_text(encoding="utf-8")))


def write_baseline(entries: set[str]) -> None:
    BASELINE_PATH.write_text(json.dumps(sorted(entries), indent=2) + "\n", encoding="utf-8")


def report_edge_counts(violations: list[Include]) -> None:
    for (source, target), count in sorted(count_edges(violations).items()):
        print(f"  {source} -> {target}: {count}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; fail when docs/architecture.md is out of date")
    parser.add_argument(
        "--update-baseline", action="store_true", help="remove the baseline entries that are gone; never adds one"
    )
    arguments = parser.parse_args()

    includes = build_includes(read_tracked_sources())
    violations = find_violations(includes)
    baseline = read_baseline()
    new_violations, stale_entries = compare_with_baseline(violations, baseline)
    status = 0

    diagram = render_diagram(includes)
    if arguments.check:
        if not DIAGRAM_PATH.exists() or DIAGRAM_PATH.read_text(encoding="utf-8") != diagram:
            print("docs/architecture.md is out of date; run: python tools/include_layers.py")
            status = 1
    else:
        DIAGRAM_PATH.write_text(diagram, encoding="utf-8")

    if arguments.update_baseline:
        if not BASELINE_PATH.exists():
            write_baseline({item.key for item in violations})
            new_violations = []
        elif new_violations:
            print("refusing to update the baseline: there are new violations")
        else:
            write_baseline(baseline - set(stale_entries))
            stale_entries = []

    for item in new_violations:
        print(f"{item.source_file}:{item.line}: includes {item.included_path}, "
              f"which is a higher layer ({item.source_node} -> {item.target_node})")
        status = 1
    for entry in stale_entries:
        print(f"baseline lists a violation that is gone, remove it (--update-baseline): {entry}")
        status = 1

    print(f"{len(violations)} known upward includes, {len(new_violations)} new, {len(stale_entries)} gone")
    if status == 0:
        report_edge_counts(violations)
    return status


if __name__ == "__main__":
    sys.exit(main())

# MCP Server (`nodalys mcp`)

**Implementation files:**
- [`nodalys/src/mcp/mcp_protocol.cpp`](../../nodalys/src/mcp/mcp_protocol.cpp) (JSON-RPC 2.0 and the MCP handshake, apart from the tools)
- [`nodalys/src/mcp/mcp_server.cpp`](../../nodalys/src/mcp/mcp_server.cpp) (the loop over a pair of streams)
- [`nodalys/src/mcp/tool_check_netlist.cpp`](../../nodalys/src/mcp/tool_check_netlist.cpp), [`tool_simulate.cpp`](../../nodalys/src/mcp/tool_simulate.cpp), [`tool_list_models.cpp`](../../nodalys/src/mcp/tool_list_models.cpp)
- [`nodalys/src/mcp/simulation_request.cpp`](../../nodalys/src/mcp/simulation_request.cpp) (the arguments of `simulate` over the directives of the netlist), [`simulation_summary.cpp`](../../nodalys/src/mcp/simulation_summary.cpp) and [`signal_measures.cpp`](../../nodalys/src/mcp/signal_measures.cpp) (the compact figures)
- [`nodalys/src/cli/analysis_runner.cpp`](../../nodalys/src/cli/analysis_runner.cpp) (runs one analysis of any kind, shared with the command line)
- [`nodalys/tests/mcp_server_tests.cpp`](../../nodalys/tests/mcp_server_tests.cpp), [`nodalys/tests/signal_measures_tests.cpp`](../../nodalys/tests/signal_measures_tests.cpp)

`nodalys mcp` runs the command line as a [Model Context Protocol](https://modelcontextprotocol.io) server, so an AI assistant can check netlists and run analyses on the Nodalys engine and read the answer as a few figures, instead of working a circuit out in its head or reading thousands of samples.

It speaks MCP over standard input and output only, with no network and no key: the host starts `nodalys mcp` as a child process and the two talk through its pipes. Each message is one line of JSON-RPC 2.0 with no line break inside it, as the MCP stdio transport specifies; the log lines go to standard error. The server answers the revisions 2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05 of the specification (the client's version when it is one of these, the newest otherwise) and offers the tools capability.

The JSON-RPC layer uses nlohmann/json, which only the command line links; the engine library keeps depending on nothing.

---

## 1. Registering it with an AI host

The command is `nodalys mcp`, so `nodalys` must be on the `PATH` of the host (the AUR package, the AppImage and the Windows zip carry it beside `imcsim`), or its full path is given.

**Claude Code**, in a `.mcp.json` at the root of a project, or with `claude mcp add nodalys -- nodalys mcp`:

```json
{
  "mcpServers": {
    "nodalys": {
      "command": "nodalys",
      "args": ["mcp"]
    }
  }
}
```

**Claude Desktop** takes the same `mcpServers` entry in its `claude_desktop_config.json`. Any other host that starts a stdio MCP server uses the same two fields.

To try it by hand, pipe lines into it:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"hand","version":"1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | nodalys mcp
```

---

## 2. The protocol

| Message | Answer |
| :--- | :--- |
| `initialize` | `protocolVersion`, `capabilities.tools`, `serverInfo` (`nodalys` and its version) and a short `instructions` text for the AI. |
| `notifications/initialized` and any other notification | None, as JSON-RPC asks. |
| `ping` | An empty result. |
| `tools/list` | The three tools with their descriptions and JSON Schemas. |
| `tools/call` | The result of the tool: one text content block holding JSON, with `isError`. |

Protocol failures are JSON-RPC errors: `-32700` for a line that is not JSON (with a null `id`), `-32600` for a message that is not a valid request or is a batch (the revisions after 2025-03-26 dropped batches), `-32601` for an unknown method, `-32602` for an unknown tool or arguments that are not an object, `-32603` for a bug inside a tool.

A failure of the tool's own work is not a protocol error: a bad netlist, a wrong argument or a run that does not converge comes back as a normal result with `isError` true and the reason as its text, so the AI can read it and correct the call.

---

## 3. Tools

### 3.1 `check_netlist`

Reads a netlist with the reader of the command line (`ParseNetlist()`, with scoped subcircuits) and runs nothing. Argument: `netlist`, the text of the netlist, whose first line is its title.

It answers `valid`, `title`, the `nodes` named in the circuit (lower case, subcircuit internals left out), `elements` (the count per first letter of the top-level elements, so `{"C":1,"R":1,"V":1}`), the `subcircuits` defined, the `analyses` the directives ask for (`op` when there is none), `mna_unknowns` and the `warnings` of the reader, such as an ignored line. A mistake comes back as an error that names the line or directive.

### 3.2 `simulate`

Runs one analysis and answers its figures.

| Argument | Meaning |
| :--- | :--- |
| `netlist` | The netlist text (required). |
| `analysis` | `op`, `tran`, `ac`, `dc`, `noise`, `tf`, `sens` (DC), `sens_ac`, `pz`, `sp` or `hb`. Omitted: the first directive of the netlist, `.op` when it has none. |
| `nodes` | Names of the nodes to report, in any case, `v(out)` or `out`; ground is `0`. Omitted: the named nodes of the circuit, up to 24 (`nodes_omitted` counts the rest). |
| `detail` | `summary` (default) or `full`. |
| `output_file` | Path of a file the full data is written to; the answer stays a summary. |
| `stop_time`, `time_step` | `tran`. |
| `start_frequency`, `stop_frequency`, `points`, `sweep` | `ac`, `noise`, `sens_ac` (points per decade or octave, or in total for `lin`) and `sp`; `sweep` is `dec`, `oct` or `lin`. |
| `source`, `start`, `stop`, `step` | `dc`. |
| `output_node`, `reference_node`, `input_source` | `tf`, `noise`, `pz`, `sens`, `sens_ac`: the output voltage, the negative side of it (ground when omitted) and the source that drives the circuit (`V1`). |
| `fundamental_frequency`, `harmonics` | `hb`. |

Numbers are JSON numbers or SPICE strings (`"1k"`, `"10u"`). With `analysis`, the settings start from the netlist's directive of the same kind when there is one, and the arguments replace them; without a directive the arguments give them, and a missing required one is an error that names it. A netlist with several directives runs the first, and the answer lists the rest in `netlist_analyses`.

What each analysis answers, per requested node unless said otherwise (figures have six significant digits):

| Analysis | Figures |
| :--- | :--- |
| `op` | `node_voltages_v`, `currents_a` (the first 40, with `currents_omitted`), `warnings`. |
| `tran` | `points`, `start_time_s`, `stop_time_s`, `is_complete`; per node `min_v`, `max_v`, `peak_to_peak_v`, `final_v`, `mean_v`, `rms_v`, the mean and the RMS weighted by the time step so a variable step does not skew them. |
| `ac` | The `frequencies` range; per node the gain and phase at the two ends, `peak_gain_db` and `peak_frequency_hz`, `lower_cutoff_3db_hz` and `upper_cutoff_3db_hz` (3 dB under the peak, interpolated on a logarithmic axis, null when the sweep does not reach them), `unity_gain_hz`, `phase_at_unity_gain_deg` and `phase_margin_deg` (180 plus that phase, meaningful when the node is a loop gain). |
| `dc` | The swept source and range; per node `min_v` and `max_v` with the sweep value of each, and `first_v`, `last_v`. For a second, stepped source the extremes cover all the curves. |
| `noise` | `output_total` and `input_total` (integrated), the densities at the two ends and the five `largest_contributors`. |
| `tf` | `gain`, `input_impedance_ohm`, `output_impedance_ohm`. |
| `sens` | The ten `largest_sensitivities` and the element count (all elements are in `detail: full`). |
| `sens_ac` | The ten elements with the largest `peak_gain_change_db_per_percent` and the frequency of that peak. |
| `pz` | `poles` and `zeros` (up to 24 each) with `real` and `imag` in rad/s and `frequency_hz`. |
| `sp` | The ports and their reference impedances; per entry `S11`, `S21`... the minimum and maximum magnitude in dB and the frequency of the maximum. |
| `hb` | The fundamental, the iterations and the strategy; per node `dc_v`, the amplitudes of harmonics 1 to 5 and `thd_percent`. |

The answer never holds a vector of points, so a transient of a hundred thousand points is as short as one of a hundred (the test keeps it under 1500 characters). Only the solver messages that flag an error are repeated; the others are progress lines.

`detail: full` adds `full`: `node_names`, the name of each node number, and `data`, the result in the JSON export format of the command line (`nodalys --format json`), whose nodes are numbers. It is refused, with a note in `full_data_omitted`, above 100000 characters; `output_file` has no limit and writes the same object, `node_names` and `data`, to the path given.

### 3.3 `list_models`

Lists what the netlist reader accepts, so an AI knows what it may write: the elements with their line syntax, the source waveforms, the `.model` types and parameters, the directives, the analyses with the `simulate` arguments of each and the expression language of the behavioral source. Argument: `topic`, one of `elements`, `sources`, `models`, `directives`, `analyses`, `expressions`, to answer one part; omitted, it answers everything and adds a few general rules of the netlist format.

The catalog is one table in [`tool_list_models.cpp`](../../nodalys/src/mcp/tool_list_models.cpp), apart from the analysis names, which come from the same table `simulate` reads its `analysis` from. Keep it in step with the netlist parser when an element, a waveform or a model parameter is added.

---

## 4. Limits

- One request at a time: the server reads a line, runs the tool and answers before it reads the next. A `notifications/cancelled` is ignored, so a long transient cannot be stopped from the host; the host can kill the process.
- `output_file` writes where the caller says: register the server only with hosts you trust to name paths, as for any tool that writes files.
- The nodes the AI can name are those the netlist names; the probes and internal nodes the engine adds are not reachable.

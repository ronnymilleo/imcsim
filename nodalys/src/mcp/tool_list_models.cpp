/**
 * @file    tool_list_models.cpp
 * @brief   The list_models tool: what devices, sources, models, directives and analyses the netlist reader accepts.
 */

#include "mcp/tool_list_models.h"

#include "mcp/analysis_names.h"
#include "mcp/json_arguments.h"
#include "nodalys/version.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Nodalys {

namespace {

constexpr const char *DESCRIPTION =
    "Lists what the Nodalys netlist reader accepts, so you know what you may write: the circuit elements with their "
    "line syntax, the source waveforms, the .model cards and their parameters, the directives, the analyses with the "
    "arguments 'simulate' takes for each, and the expression language of behavioral sources. Call it before writing a "
    "netlist with an unfamiliar device. 'topic' limits the answer to one part: elements, sources, models, directives, "
    "analyses or expressions; omit it for everything.";

constexpr std::array<std::string_view, 6> TOPICS = {"elements",   "sources",  "models",
                                                    "directives", "analyses", "expressions"};

nlohmann::json MakeInputSchema() {
    return {{"type", "object"},
            {"properties",
             {{"topic",
               {{"type", "string"},
                {"enum", TOPICS},
                {"description", "Part of the catalog to answer; omit for all of it."}}}}},
            {"additionalProperties", false}};
}

nlohmann::json Element(const std::string &letter, const std::string &name, const std::string &syntax,
                       const std::string &notes) {
    return {{"letter", letter}, {"name", name}, {"syntax", syntax}, {"notes", notes}};
}

/// The element lines the netlist reader accepts; the first letter of the name picks the kind.
nlohmann::json ListElements() {
    return {
        Element("R", "Resistor", "R<name> n1 n2 value", "Ohms."),
        Element("C", "Capacitor", "C<name> n1 n2 value [ic=v0]",
                "Farads; ic is the start voltage with a transient that uses initial conditions."),
        Element("L", "Inductor", "L<name> n1 n2 value [ic=i0]", "Henries; ic is the start current."),
        Element("V", "Independent voltage source", "V<name> n+ n- [DC v] [AC mag [phase]] [waveform]",
                "Also the way to measure a current: a 0 V source in series gives its branch current. See 'sources'."),
        Element("I", "Independent current source", "I<name> n+ n- [DC i] [AC mag [phase]] [waveform]",
                "Current flows from n+ through the source to n-."),
        Element("D", "Diode", "D<name> anode cathode model [area]",
                "Needs a .model of type D; Zener and LED through BV and IS, N."),
        Element("Q", "Bipolar transistor", "Q<name> collector base emitter model [area]",
                "Needs a .model of type NPN or PNP (Gummel-Poon)."),
        Element("M", "MOSFET", "M<name> drain gate source bulk model [L=l] [W=w]",
                "Needs a .model of type NMOS or PMOS (level 1, Shichman-Hodges)."),
        Element("S", "Voltage-controlled switch", "S<name> a1 a2 c1 c2 model",
                "Needs a .model of type SW or VSWITCH, with hysteresis."),
        Element("E", "Voltage-controlled voltage source (VCVS)", "E<name> n+ n- c+ c- gain", ""),
        Element("G", "Voltage-controlled current source (VCCS)", "G<name> n+ n- c+ c- transconductance", ""),
        Element("H", "Current-controlled voltage source (CCVS)", "H<name> n+ n- Vcontrol transresistance",
                "Vcontrol is the name of the voltage source the controlling current flows through."),
        Element("F", "Current-controlled current source (CCCS)", "F<name> n+ n- Vcontrol gain", "Same control as H."),
        Element("B", "Behavioral source", "B<name> n+ n- V=<expression>  or  B<name> n+ n- I=<expression>",
                "See 'expressions'. Not allowed inside a .subckt."),
        Element("X", "Subcircuit instance", "X<name> node1 node2 ... <subckt_name> [param=value ...]",
                "Needs a .subckt definition, which may nest and take parameters and {expressions}."),
        Element("U", "Logic gate (smooth continuous model)",
                "U<name> out in1 [in2 ...] <AND|NAND|OR|NOR|XOR|XNOR|NOT|BUF|SCHMITT> [VHIGH=5] [VLOW=0] [VTH=2.5] "
                "[TPD=1n] [ROUT=50] [HYST=0.5]",
                "Output node first, then the inputs, then the gate type."),
        Element("P", "Port for S-parameters", "P<name> n+ n- [portnum=N] [z0=ohms]",
                "Also a V source with portnum=N z0=Z. The sp analysis needs at least one port."),
    };
}

nlohmann::json ListSources() {
    return {
        {"dc_and_ac",
         "V1 in 0 DC 5 AC 1 0   (AC magnitude and phase in degrees are used by ac, noise, sens_ac and pz)"},
        {"waveforms",
         {{{"form", "PULSE(v1 v2 delay rise fall width period)"}, {"notes", "Square or trapezoid pulses; a clock."}},
          {{"form", "SIN(offset amplitude frequency [delay [damping [phase]]])"}, {"notes", "Also written SINE(...)."}},
          {{"form", "EXP(v1 v2 rise_delay rise_tau fall_delay fall_tau)"}, {"notes", ""}},
          {{"form", "PWL(t1 v1 t2 v2 ...)"}, {"notes", "Piecewise linear; the transient lands on each corner."}},
          {{"form", "SFFM(offset amplitude carrier index signal)"}, {"notes", "Single-frequency FM."}},
          {{"form", "AM(amplitude offset modulating carrier [delay])"}, {"notes", "Amplitude modulation."}},
          {{"form", "TRRANDOM(type timestep delay param1 param2)"},
           {"notes",
            "Random draws (uniform, Gaussian, exponential, Poisson) in a transient, seeded by .options seed=N."}},
          {{"form", "TRNOISE(white_amplitude step 1/f_exponent 1/f_amplitude ...)"},
           {"notes", "Transient noise; held at its DC level outside a transient, seeded by .options seed=N."}}}},
    };
}

nlohmann::json ListModels() {
    return {
        {"card", ".model <name> <type> (param=value ...)"},
        {"types",
         {{{"type", "D"}, {"parameters", {"IS", "N", "RS", "BV", "IBV", "CJO", "VJ", "M", "TT", "EG", "XTI"}}},
          {{"type", "NPN / PNP"},
           {"parameters",
            {"IS", "BF", "BR", "VAF", "NE", "ISE", "IKF", "RB", "RC", "RE", "CJE", "CJC", "TF", "EG", "XTB", "XTI"}}},
          {{"type", "NMOS / PMOS"},
           {"parameters", {"VTO", "KP", "LAMBDA", "PHI", "CGSO", "CGDO", "RD", "RS", "L", "W"}}},
          {{"type", "SW / VSWITCH"}, {"parameters", {"RON", "ROFF", "VT", "VH", "VTHRESHOLD", "VHYSTERESIS"}}}}},
        {"notes",
         "Parameters not listed are ignored and named in the warnings of check_netlist. A model written inside "
         "a .subckt belongs to that subcircuit."},
    };
}

nlohmann::json ListDirectives() {
    return {
        {".op", "Operating point."},
        {".tran tstep tstop [tstart [tmax]] [uic]", "Transient."},
        {".dc src start stop step [src2 start2 stop2 step2]", "DC sweep, one or two sources."},
        {".ac dec|oct|lin points fstart fstop", "AC sweep."},
        {".tf v(out[,ref]) source", "Small-signal gain and impedances."},
        {".noise v(out[,ref]) source dec|oct|lin points fstart fstop", "Noise."},
        {".sens v(out[,ref])", "DC sensitivity."},
        {".sens v(out[,ref]) ac dec|oct|lin points fstart fstop", "AC sensitivity, exclusive to Nodalys."},
        {".pz v(out) source [pol|zer|pz]", "Poles and zeros."},
        {".sp dec|oct|lin points fstart fstop", "S-parameters of the ports."},
        {".hb f0 harmonics [samples=M]", "Harmonic balance, exclusive to Nodalys."},
        {".model name type (params)", "Device model card."},
        {".subckt name nodes... [params] / .ends",
         "Subcircuit definition, nestable, with parameters and {expressions}."},
        {".param name=value", "Parameter for expressions in braces."},
        {".options reltol= vntol= abstol= gmin= itl1= temp= tnom= method=gear|trap seed=", "Solver options."},
        {".temp celsius", "Circuit temperature."},
        {".ic v(node)=value ...", "Initial node voltages."},
        {".nodeset v(node)=value ...", "Starting guesses for the operating point."},
    };
}

/// The analyses, with the names the tools use; the arguments are those of 'simulate'.
nlohmann::json ListAnalyses() {
    nlohmann::json analyses = {
        {"op", "Node voltages and branch currents at the DC operating point. No arguments."},
        {"tran", "Time response. Arguments: stop_time, time_step."},
        {"ac", "Frequency response, gain in dB and phase. Arguments: start_frequency, stop_frequency, points, sweep. "
               "The source needs an AC magnitude."},
        {"dc", "Sweep of a source. Arguments: source, start, stop, step."},
        {"noise", "Integrated output and input noise. Arguments: output_node, reference_node, input_source, "
                  "start_frequency, stop_frequency, points, sweep."},
        {"tf",
         "Small-signal DC gain, input and output impedance. Arguments: output_node, reference_node, input_source."},
        {"sens", "DC sensitivity of an output voltage to every element value. Arguments: output_node, reference_node."},
        {"sens_ac", "AC sensitivity in dB and degrees per 1 % change of each element. Arguments: output_node, "
                    "reference_node, input_source, start_frequency, stop_frequency, points, sweep."},
        {"pz", "Poles and zeros in rad/s. Arguments: output_node, input_source."},
        {"sp", "S-parameters of the circuit's ports. Arguments: start_frequency, stop_frequency, points, sweep. The "
               "netlist needs ports."},
        {"hb", "Periodic steady state by harmonic balance. Arguments: fundamental_frequency, harmonics."},
    };
    analyses["names"] = nlohmann::json::array();
    for (const std::string_view name : GetAnalysisNames()) {
        analyses["names"].push_back(std::string(name));
    }
    return analyses;
}

nlohmann::json ListExpressions() {
    return {
        {"where", "B<name> n+ n- V=<expression> or I=<expression>"},
        {"inputs",
         "v(node), v(node, reference) and i(Vsource), with node names as written in the netlist; time and temper."},
        {"operators", "+ - * / ^ ** unary - + !, comparisons < > <= >= == != <>, && ||, and a ? b : c"},
        {"functions",
         "abs, sqrt, exp, ln, log, log10, sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, asinh, "
         "acosh, atanh, sgn, u, u2, uramp, floor, ceil, nint, min, max, pow, pwr"},
        {"refused", "limit(), the random functions (gauss, agauss, unif, aunif, rnd, random, exponential) and hertz "
                    "are refused by name before the run."},
    };
}

nlohmann::json MakeCatalog(const std::optional<std::string> &topic) {
    nlohmann::json catalog = {{"nodalys_version", NODALYS_VERSION_STRING}};
    const auto wants = [&](const std::string_view name) { return !topic || *topic == name; };
    if (wants("elements")) {
        catalog["elements"] = ListElements();
    }
    if (wants("sources")) {
        catalog["sources"] = ListSources();
    }
    if (wants("models")) {
        catalog["models"] = ListModels();
    }
    if (wants("directives")) {
        catalog["directives"] = ListDirectives();
    }
    if (wants("analyses")) {
        catalog["analyses"] = ListAnalyses();
    }
    if (wants("expressions")) {
        catalog["expressions"] = ListExpressions();
    }
    if (!topic) {
        catalog["general"] = {
            "The first line of a netlist is its title, not an element.",
            "Names are case-insensitive; node 0, gnd and ground are ground.",
            "Numbers take SPICE suffixes: f p n u m k meg g t (1k, 4.7u, 100n, 1meg); m is milli, meg is mega.",
            "A line starting with + continues the previous one; * or ; starts a comment.",
            "Every circuit needs a path to ground for each node."};
    }
    return catalog;
}

ToolResult RunListModels(const nlohmann::json &arguments) {
    ArgumentReader reader(arguments);
    const std::optional<std::string> topic = reader.GetString("topic");
    if (reader.HasError()) {
        return MakeErrorResult(reader.GetError());
    }
    if (topic && std::ranges::find(TOPICS, *topic) == TOPICS.end()) {
        return MakeErrorResult("Argument 'topic' must be one of: elements, sources, models, directives, analyses, "
                               "expressions");
    }
    return MakeJsonResult(MakeCatalog(topic));
}

} // namespace

/**
 * @return  The list_models tool.
 */
McpTool MakeListModelsTool() {
    return {.Name = "list_models", .Description = DESCRIPTION, .InputSchema = MakeInputSchema(), .Run = RunListModels};
}

} // namespace Nodalys

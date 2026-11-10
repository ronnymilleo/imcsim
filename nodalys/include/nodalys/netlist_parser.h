#ifndef NODALYS_NETLIST_PARSER_H
#define NODALYS_NETLIST_PARSER_H

#include "nodalys/analyses.h"
#include "nodalys/circuit.h"
#include "nodalys/types.h"

#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nodalys {

/**
 * @enum   AnalysisKind
 * @brief  Type of simulation directive found in the SPICE netlist.
 */
enum class AnalysisKind {
    OperatingPoint,       // .op
    Transient,            // .tran
    DcSweep,              // .dc
    AcSweep,              // .ac
    TransferFunction,     // .tf
    Noise                 // .noise
};

/**
 * @struct ParsedAnalysis
 * @brief  Analysis directive and its configured parameters parsed from the netlist.
 */
struct ParsedAnalysis {
    AnalysisKind Kind = AnalysisKind::OperatingPoint;
    std::variant<
        std::monostate,         // OperatingPoint (no parameters)
        TransientConfig,        // Transient (.tran)
        DcSweepConfig,          // DcSweep (.dc)
        AcSweepConfig,          // AcSweep (.ac)
        TransferFunctionConfig, // TransferFunction (.tf)
        NoiseConfig             // Noise (.noise)
    > Config;
};

/**
 * @struct ParsedNetlist
 * @brief  The fully parsed netlist ready for simulation: compiled circuit, analyses, options.
 */
struct ParsedNetlist {
    Circuit CircuitInstance;
    std::string Title;
    std::vector<ParsedAnalysis> Analyses;
    ConvergenceOptions Options;
    std::vector<DiagnosticMessage> Diagnostics;
};

/**
 * @brief  Parses standard SPICE netlist text into a compiled circuit and analysis specifications.
 * @param  netlist_text  SPICE netlist content (e.g. from file or string).
 * @return ParsedNetlist on success, or an error description string on failure.
 */
std::expected<ParsedNetlist, std::string> ParseNetlist(std::string_view netlist_text);

/**
 * @brief  Parses a SPICE numeric literal with engineering scale suffixes (e.g. "4.7k", "10Meg", "100nF", "1e-3").
 * @param  text  Input string to parse.
 * @return The parsed double value, or std::nullopt if the text is not a valid number.
 */
std::optional<double> ParseSpiceNumber(std::string_view text);

} // namespace nodalys

#endif // NODALYS_NETLIST_PARSER_H

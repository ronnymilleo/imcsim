/**
 * @file    netlist_parser.h
 * @brief   Parses SPICE netlists into compiled circuits and simulation requests.
 */

#ifndef NODALYS_NETLIST_PARSER_H
#define NODALYS_NETLIST_PARSER_H

#include "nodalys/analyses.h"
#include "nodalys/circuit.h"
#include "nodalys/types.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Nodalys {

/**
 * @enum   AnalysisKind
 * @brief  Type of simulation directive found in the SPICE netlist.
 */
enum class AnalysisKind {
    OPERATING_POINT,   ///< .op
    TRANSIENT,         ///< .tran
    DC_SWEEP,          ///< .dc
    AC_SWEEP,          ///< .ac
    TRANSFER_FUNCTION, ///< .tf
    NOISE,             ///< .noise
    S_PARAMETERS,      ///< .sp
    POLE_ZERO,         ///< .pz
    SENSITIVITY,       ///< .sens
    HARMONIC_BALANCE   ///< .hb
};

/**
 * @struct ParsedAnalysis
 * @brief  Analysis directive and its configured parameters parsed from the netlist.
 */
struct ParsedAnalysis {
    AnalysisKind Kind = AnalysisKind::OPERATING_POINT;
    std::variant<std::monostate,         // OperatingPoint (no parameters)
                 TransientConfig,        // Transient (.tran)
                 DcSweepConfig,          // DcSweep (.dc)
                 AcSweepConfig,          // AcSweep (.ac)
                 TransferFunctionConfig, // TransferFunction (.tf)
                 NoiseConfig,            // Noise (.noise)
                 SParameterConfig,       // SParameters (.sp)
                 PoleZeroConfig,         // PoleZero (.pz)
                 SensitivityConfig,      // Sensitivity (.sens)
                 HarmonicBalanceConfig   // HarmonicBalance (.hb)
                 >
        Config;
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

// Parsing
[[nodiscard]] std::expected<ParsedNetlist, std::string> ParseNetlist(std::string_view netlist_text);
[[nodiscard]] std::optional<double> ParseSpiceNumber(std::string_view text);

} // namespace Nodalys

#endif // NODALYS_NETLIST_PARSER_H

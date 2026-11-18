/**
 * @file    cli.cpp
 * @brief   The command line of the standalone Nodalys tool: reads a netlist, runs its analyses and writes the results.
 */

#include "cli/cli.h"

#include "nodalys/nodalys.h"
#include "nodalys/version.h"
#include "text/text_case.h"

#include <CLI/CLI.hpp>
#include <algorithm>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace Nodalys {

namespace {

/// Whether an analysis of the netlist is the one that `--analysis` names.
bool IsOverrideMatch(const std::string &name, const AnalysisKind kind) {
    if (name == "op") {
        return kind == AnalysisKind::OPERATING_POINT;
    }
    if (name == "tran") {
        return kind == AnalysisKind::TRANSIENT;
    }
    if (name == "dc") {
        return kind == AnalysisKind::DC_SWEEP;
    }
    if (name == "ac") {
        return kind == AnalysisKind::AC_SWEEP;
    }
    if (name == "tf") {
        return kind == AnalysisKind::TRANSFER_FUNCTION;
    }
    if (name == "noise") {
        return kind == AnalysisKind::NOISE;
    }
    if (name == "sp") {
        return kind == AnalysisKind::S_PARAMETERS;
    }
    if (name == "pz") {
        return kind == AnalysisKind::POLE_ZERO;
    }
    if (name == "sens") {
        // Both forms of .sens
        return kind == AnalysisKind::SENSITIVITY || kind == AnalysisKind::AC_SENSITIVITY;
    }
    if (name == "hb") {
        return kind == AnalysisKind::HARMONIC_BALANCE;
    }
    return false;
}

std::optional<std::string> ReadFileContent(const std::string &path, std::istream &input) {
    if (path == "-") {
        std::ostringstream ss;
        ss << input.rdbuf();
        return ss.str();
    }
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

/**
 * @brief   Runs the command line: parses the arguments, reads the netlist, runs its analyses and writes the results.
 * @param[in] argc         Number of arguments, the program name included.
 * @param[in] argv         Arguments, the program name first.
 * @param[in,out] input    Stream the netlist is read from when its path is "-".
 * @param[in,out] output   Stream of the results when no output file is given, and of the help and progress text.
 * @param[in,out] errors   Stream of the error messages.
 * @return  EXIT_SUCCESS, or EXIT_FAILURE when the arguments, the netlist or an analysis fails; asking for the help
 *          or the version succeeds.
 */
int RunCli(const int argc, const char *const *argv, std::istream &input, std::ostream &output, std::ostream &errors) {
    CLI::App app{"Nodalys - Autonomous SPICE Circuit Simulator\n"
                 "High-performance autonomous MNA simulation engine with zero external dependencies."};

    std::string input_file;
    std::string output_file;
    std::string format_str = "raw";
    std::string analysis_override;
    bool batch_mode = false;
    bool verbose = false;

    std::optional<double> step_override;
    std::optional<double> stop_override;
    std::optional<double> gmin_override;
    std::optional<double> reltol_override;
    std::optional<double> abstol_override;
    std::optional<double> vntol_override;
    std::optional<int> maxiter_override;

    // Command-line arguments
    app.add_option("netlist", input_file, "Path to SPICE netlist file (.cir, .sp, .net) or '-' for stdin");
    app.add_flag("-b,--batch", batch_mode, "Run in batch mode (suppress interactive messages)");
    app.add_option("-o,--output", output_file, "Destination output file (default: stdout)");
    app.add_option("-f,--format", format_str, "Output format: raw (Nutmeg ASCII), csv, json, touchstone (default: raw)")
        ->check(CLI::IsMember({"raw", "raw-ascii", "csv", "json", "touchstone", "s2p"}, CLI::ignore_case));
    app.add_option("-a,--analysis",
                   analysis_override,
                   "Override analysis: op, tran, dc, ac, tf, noise, sp, pz, sens (DC and AC), hb")
        ->check(CLI::IsMember({"op", "tran", "dc", "ac", "tf", "noise", "sp", "pz", "sens", "hb"}, CLI::ignore_case));
    app.add_option("-s,--step", step_override, "Override transient step time / sweep step");
    app.add_option("-t,--stop", stop_override, "Override transient stop time");

    // Solver tolerance options
    app.add_option("--gmin", gmin_override, "Minimum conductance to ground in Siemens (default: 1e-12)");
    app.add_option("--reltol", reltol_override, "Relative tolerance for Newton-Raphson (default: 1e-3)");
    app.add_option("--abstol", abstol_override, "Absolute current tolerance in Amperes (default: 1e-12)");
    app.add_option("--vntol", vntol_override, "Absolute voltage tolerance in Volts (default: 1e-6)");
    app.add_option("--maxiter", maxiter_override, "Maximum Newton-Raphson iterations per step (default: 100)");

    app.add_flag("-v,--verbose", verbose, "Enable verbose output (solver diagnostics and timings)");
    app.set_version_flag("--version",
                         std::format("nodalys {} (imcsim engine, standalone SPICE simulator)", NODALYS_VERSION_STRING));

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        // CLI11 reports a parse failure, and a request for the help or the version, by throwing
        return app.exit(error, output, errors);
    }

    if (input_file.empty()) {
        errors << "Error: No input netlist specified. Run 'nodalys --help' for usage.\n";
        return EXIT_FAILURE;
    }

    const auto content_opt = ReadFileContent(input_file, input);
    if (!content_opt) {
        errors << std::format("Error: Cannot open netlist file '{}'\n", input_file);
        return EXIT_FAILURE;
    }

    if (verbose && !batch_mode) {
        output << std::format("Nodalys Engine v{}\n", NODALYS_VERSION_STRING);
        output << std::format("Parsing netlist: {}\n", input_file == "-" ? "<stdin>" : input_file);
    }

    const auto parse_start = std::chrono::steady_clock::now();
    auto parsed_res = Nodalys::ParseNetlist(*content_opt);
    const auto parse_end = std::chrono::steady_clock::now();

    if (!parsed_res) {
        errors << std::format("Parse Error: {}\n", parsed_res.error());
        return EXIT_FAILURE;
    }

    auto parsed = std::move(*parsed_res);

    if (verbose && !batch_mode) {
        const auto parse_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();
        output << std::format("Netlist parsed in {:.2f} ms: {} nodes, {} MNA unknowns\n",
                              parse_ms,
                              parsed.CircuitInstance.GetNodeCount(),
                              parsed.CircuitInstance.GetUnknownCount());
    }

    // Apply tolerance overrides if provided
    if (gmin_override) {
        parsed.Options.Gmin = *gmin_override;
    }
    if (reltol_override) {
        parsed.Options.RelTol = *reltol_override;
    }
    if (abstol_override) {
        parsed.Options.CurrentAbsTol = *abstol_override;
    }
    if (vntol_override) {
        parsed.Options.VoltageAbsTol = *vntol_override;
    }
    if (maxiter_override) {
        parsed.Options.MaxIterations = *maxiter_override;
    }

    // Apply analysis overrides
    if (!analysis_override.empty()) {
        const std::string a_lower = ToLower(analysis_override);
        const auto it =
            std::ranges::find_if(parsed.Analyses, [&](const auto &an) { return IsOverrideMatch(a_lower, an.Kind); });

        if (it != parsed.Analyses.end()) {
            std::vector<Nodalys::ParsedAnalysis> matches;
            std::ranges::copy_if(parsed.Analyses, std::back_inserter(matches), [&](const auto &an) {
                return IsOverrideMatch(a_lower, an.Kind);
            });
            // Only a sensitivity keeps every match, since a netlist may ask for both of its forms
            if (a_lower != "sens") {
                matches.resize(1);
            }
            parsed.Analyses = std::move(matches);
        } else {
            parsed.Analyses.clear();
            if (a_lower == "op") {
                parsed.Analyses.push_back({Nodalys::AnalysisKind::OPERATING_POINT, std::monostate{}});
            } else if (a_lower == "tran") {
                Nodalys::TransientConfig cfg;
                cfg.TimeStep = step_override.value_or(1e-6);
                cfg.StopTime = stop_override.value_or(1e-3);
                cfg.MaxStep = cfg.TimeStep;
                parsed.Analyses.push_back({Nodalys::AnalysisKind::TRANSIENT, cfg});
            } else if (a_lower == "sp") {
                parsed.Analyses.push_back({Nodalys::AnalysisKind::S_PARAMETERS, Nodalys::SParameterConfig{}});
            } else if (a_lower == "pz") {
                parsed.Analyses.push_back({Nodalys::AnalysisKind::POLE_ZERO, Nodalys::PoleZeroConfig{}});
            } else if (a_lower == "sens") {
                parsed.Analyses.push_back({Nodalys::AnalysisKind::SENSITIVITY, Nodalys::SensitivityConfig{}});
            } else if (a_lower == "hb") {
                parsed.Analyses.push_back({Nodalys::AnalysisKind::HARMONIC_BALANCE, Nodalys::HarmonicBalanceConfig{}});
            }
        }
    }

    if (parsed.Analyses.empty()) {
        parsed.Analyses.push_back({Nodalys::AnalysisKind::OPERATING_POINT, std::monostate{}});
    }

    std::vector<Nodalys::DiagnosticMessage> diagnostics;
    std::string all_exported_output;

    const std::string fmt_lower = ToLower(format_str);
    const std::string sim_title = parsed.Title.empty() ? (input_file == "-" ? "Simulation" : input_file) : parsed.Title;

    for (std::size_t a_idx = 0; a_idx < parsed.Analyses.size(); ++a_idx) {
        auto &analysis = parsed.Analyses[a_idx];
        const auto sim_start = std::chrono::steady_clock::now();

        Nodalys::SimulationResult sim_result;

        switch (analysis.Kind) {
        case Nodalys::AnalysisKind::OPERATING_POINT: {
            if (verbose && !batch_mode) {
                output << "Running Operating Point (.op) analysis...\n";
            }
            auto res = Nodalys::ComputeOperatingPoint(parsed.CircuitInstance,
                                                      parsed.Options,
                                                      verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::TRANSIENT: {
            auto &cfg = std::get<Nodalys::TransientConfig>(analysis.Config);
            if (step_override) {
                cfg.TimeStep = *step_override;
            }
            if (stop_override) {
                cfg.StopTime = *stop_override;
            }
            if (cfg.MaxStep <= 0.0) {
                cfg.MaxStep = cfg.TimeStep;
            }
            if (verbose && !batch_mode) {
                output << std::format("Running Transient (.tran) analysis: tstep={}, tstop={}\n",
                                      cfg.TimeStep,
                                      cfg.StopTime);
            }
            auto res = Nodalys::ComputeTransient(parsed.CircuitInstance,
                                                 cfg,
                                                 cfg.UseInitialConditions,
                                                 {},
                                                 parsed.Options,
                                                 verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::DC_SWEEP: {
            const auto &cfg = std::get<Nodalys::DcSweepConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running DC Sweep (.dc) analysis on source {}\n", cfg.SweptSource);
            }
            auto res =
                Nodalys::ComputeDCSweep(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::AC_SWEEP: {
            const auto &cfg = std::get<Nodalys::AcSweepConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running AC Sweep (.ac) analysis: {} Hz to {} Hz\n",
                                      cfg.StartFrequency,
                                      cfg.StopFrequency);
            }
            auto res =
                Nodalys::ComputeACSweep(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::TRANSFER_FUNCTION: {
            const auto &cfg = std::get<Nodalys::TransferFunctionConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running Transfer Function (.tf) analysis for source {}\n", cfg.InputSource);
            }
            auto res = Nodalys::ComputeTransferFunction(parsed.CircuitInstance,
                                                        cfg,
                                                        parsed.Options,
                                                        verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::NOISE: {
            const auto &cfg = std::get<Nodalys::NoiseConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running Noise (.noise) analysis for source {}\n", cfg.InputSource);
            }
            auto res =
                Nodalys::ComputeNoise(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::S_PARAMETERS: {
            const auto &cfg = std::get<Nodalys::SParameterConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running S-Parameter (.sp) analysis: {} Hz to {} Hz\n",
                                      cfg.StartFrequency,
                                      cfg.StopFrequency);
            }
            auto res = Nodalys::ComputeSParameters(parsed.CircuitInstance,
                                                   cfg,
                                                   parsed.Options,
                                                   verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::POLE_ZERO: {
            const auto &cfg = std::get<Nodalys::PoleZeroConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running Pole-Zero (.pz) analysis: output={}, input={}\n",
                                      cfg.OutputNode,
                                      cfg.InputSource);
            }
            auto res =
                Nodalys::ComputePoleZero(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::SENSITIVITY: {
            const auto &cfg = std::get<Nodalys::SensitivityConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running DC Sensitivity (.sens) analysis: pos={}, neg={}\n",
                                      cfg.OutputPositiveNode,
                                      cfg.OutputNegativeNode);
            }
            auto res = Nodalys::ComputeSensitivity(parsed.CircuitInstance,
                                                   cfg,
                                                   parsed.Options,
                                                   verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::AC_SENSITIVITY: {
            const auto &cfg = std::get<Nodalys::AcSensitivityConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running AC Sensitivity (.sens ... ac) analysis: pos={}, neg={}, input={}\n",
                                      cfg.OutputPositiveNode,
                                      cfg.OutputNegativeNode,
                                      cfg.InputSource);
            }
            auto res = Nodalys::ComputeAcSensitivity(parsed.CircuitInstance,
                                                     cfg,
                                                     parsed.Options,
                                                     verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case Nodalys::AnalysisKind::HARMONIC_BALANCE: {
            const auto &cfg = std::get<Nodalys::HarmonicBalanceConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                output << std::format("Running Harmonic Balance (.hb) analysis: f0={} Hz, H={}\n",
                                      cfg.FundamentalFrequency,
                                      cfg.Harmonics);
            }
            auto res = Nodalys::ComputeHarmonicBalance(parsed.CircuitInstance,
                                                       cfg,
                                                       parsed.Options,
                                                       verbose ? &diagnostics : nullptr);
            if (!res) {
                errors << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }
        }

        const auto sim_end = std::chrono::steady_clock::now();
        if (verbose && !batch_mode) {
            const auto sim_ms = std::chrono::duration<double, std::milli>(sim_end - sim_start).count();
            output << std::format("Analysis completed in {:.2f} ms\n", sim_ms);
        }

        // Export result
        std::string exported;
        if (fmt_lower == "csv") {
            exported = Nodalys::ExportCsv(sim_result);
        } else if (fmt_lower == "json") {
            exported = Nodalys::ExportJson(sim_result);
        } else if (fmt_lower == "touchstone" || fmt_lower == "s2p") {
            if (std::holds_alternative<Nodalys::SParameterResult>(sim_result)) {
                exported = Nodalys::ExportTouchstone(std::get<Nodalys::SParameterResult>(sim_result));
            } else {
                exported = Nodalys::ExportCsv(sim_result);
            }
        } else {
            // raw or raw-ascii
            exported = Nodalys::ExportRaw(sim_result, sim_title);
        }

        all_exported_output.append(exported);
    }

    if (verbose && !diagnostics.empty()) {
        output << "\nDiagnostic Messages:\n";
        for (const auto &diag : diagnostics) {
            output << "  - " << diag.Text << "\n";
        }
    }

    // Write output
    if (!output_file.empty()) {
        if (!Nodalys::SaveStringToFile(output_file, all_exported_output)) {
            errors << std::format("Error: Failed to write output file '{}'\n", output_file);
            return EXIT_FAILURE;
        }
        if (verbose && !batch_mode) {
            output << std::format("Simulation output written to '{}'\n", output_file);
        }
    } else {
        output << all_exported_output;
    }

    return EXIT_SUCCESS;
}

} // namespace Nodalys

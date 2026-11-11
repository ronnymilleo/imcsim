/**
 * @file    main.cpp
 * @brief   Nodalys standalone command-line SPICE simulation tool.
 */

#include "nodalys/nodalys.h"
#include "nodalys/version.h"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace {

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (const char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::optional<std::string> ReadFileContent(const std::string &path) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
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

int main(int argc, char **argv) {
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
    app.add_option("-a,--analysis", analysis_override, "Override analysis: op, tran, dc, ac, tf, noise, sp, pz")
        ->check(CLI::IsMember({"op", "tran", "dc", "ac", "tf", "noise", "sp", "pz"}, CLI::ignore_case));
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

    CLI11_PARSE(app, argc, argv);

    if (input_file.empty()) {
        std::cerr << "Error: No input netlist specified. Run 'nodalys --help' for usage.\n";
        return EXIT_FAILURE;
    }

    const auto content_opt = ReadFileContent(input_file);
    if (!content_opt) {
        std::cerr << std::format("Error: Cannot open netlist file '{}'\n", input_file);
        return EXIT_FAILURE;
    }

    if (verbose && !batch_mode) {
        std::cout << std::format("Nodalys Engine v{}\n", NODALYS_VERSION_STRING);
        std::cout << std::format("Parsing netlist: {}\n", input_file == "-" ? "<stdin>" : input_file);
    }

    const auto parse_start = std::chrono::steady_clock::now();
    auto parsed_res = nodalys::ParseNetlist(*content_opt);
    const auto parse_end = std::chrono::steady_clock::now();

    if (!parsed_res) {
        std::cerr << std::format("Parse Error: {}\n", parsed_res.error());
        return EXIT_FAILURE;
    }

    auto parsed = std::move(*parsed_res);

    if (verbose && !batch_mode) {
        const auto parse_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();
        std::cout << std::format("Netlist parsed in {:.2f} ms: {} nodes, {} MNA unknowns\n", parse_ms,
                                 parsed.CircuitInstance.NodeCount(), parsed.CircuitInstance.UnknownCount());
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
        auto it = std::ranges::find_if(parsed.Analyses, [&](const auto &an) {
            if (a_lower == "op") {
                return an.Kind == nodalys::AnalysisKind::OperatingPoint;
            }
            if (a_lower == "tran") {
                return an.Kind == nodalys::AnalysisKind::Transient;
            }
            if (a_lower == "dc") {
                return an.Kind == nodalys::AnalysisKind::DcSweep;
            }
            if (a_lower == "ac") {
                return an.Kind == nodalys::AnalysisKind::AcSweep;
            }
            if (a_lower == "tf") {
                return an.Kind == nodalys::AnalysisKind::TransferFunction;
            }
            if (a_lower == "noise") {
                return an.Kind == nodalys::AnalysisKind::Noise;
            }
            if (a_lower == "sp") {
                return an.Kind == nodalys::AnalysisKind::SParameters;
            }
            if (a_lower == "pz") {
                return an.Kind == nodalys::AnalysisKind::PoleZero;
            }
            if (a_lower == "sens") {
                return an.Kind == nodalys::AnalysisKind::Sensitivity;
            }
            return false;
        });

        if (it != parsed.Analyses.end()) {
            nodalys::ParsedAnalysis match = *it;
            parsed.Analyses.clear();
            parsed.Analyses.push_back(std::move(match));
        } else {
            parsed.Analyses.clear();
            if (a_lower == "op") {
                parsed.Analyses.push_back({nodalys::AnalysisKind::OperatingPoint, std::monostate{}});
            } else if (a_lower == "tran") {
                nodalys::TransientConfig cfg;
                cfg.TimeStep = step_override.value_or(1e-6);
                cfg.StopTime = stop_override.value_or(1e-3);
                cfg.MaxStep = cfg.TimeStep;
                parsed.Analyses.push_back({nodalys::AnalysisKind::Transient, cfg});
            } else if (a_lower == "sp") {
                parsed.Analyses.push_back({nodalys::AnalysisKind::SParameters, nodalys::SParameterConfig{}});
            } else if (a_lower == "pz") {
                parsed.Analyses.push_back({nodalys::AnalysisKind::PoleZero, nodalys::PoleZeroConfig{}});
            } else if (a_lower == "sens") {
                parsed.Analyses.push_back({nodalys::AnalysisKind::Sensitivity, nodalys::SensitivityConfig{}});
            }
        }
    }

    if (parsed.Analyses.empty()) {
        parsed.Analyses.push_back({nodalys::AnalysisKind::OperatingPoint, std::monostate{}});
    }

    std::vector<nodalys::DiagnosticMessage> diagnostics;
    std::string all_exported_output;

    const std::string fmt_lower = ToLower(format_str);
    const std::string sim_title = parsed.Title.empty() ? (input_file == "-" ? "Simulation" : input_file) : parsed.Title;

    for (std::size_t a_idx = 0; a_idx < parsed.Analyses.size(); ++a_idx) {
        auto &analysis = parsed.Analyses[a_idx];
        const auto sim_start = std::chrono::steady_clock::now();

        nodalys::SimulationResult sim_result;

        switch (analysis.Kind) {
        case nodalys::AnalysisKind::OperatingPoint: {
            if (verbose && !batch_mode) {
                std::cout << "Running Operating Point (.op) analysis...\n";
            }
            auto res = nodalys::ComputeOperatingPoint(parsed.CircuitInstance, parsed.Options,
                                                      verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::Transient: {
            auto &cfg = std::get<nodalys::TransientConfig>(analysis.Config);
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
                std::cout << std::format("Running Transient (.tran) analysis: tstep={}, tstop={}\n", cfg.TimeStep,
                                         cfg.StopTime);
            }
            auto res = nodalys::ComputeTransient(parsed.CircuitInstance, cfg, cfg.UseInitialConditions, {},
                                                 parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::DcSweep: {
            const auto &cfg = std::get<nodalys::DcSweepConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running DC Sweep (.dc) analysis on source {}\n", cfg.SweptSource);
            }
            auto res =
                nodalys::ComputeDCSweep(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::AcSweep: {
            const auto &cfg = std::get<nodalys::AcSweepConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running AC Sweep (.ac) analysis: {} Hz to {} Hz\n", cfg.StartFrequency,
                                         cfg.StopFrequency);
            }
            auto res =
                nodalys::ComputeACSweep(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::TransferFunction: {
            const auto &cfg = std::get<nodalys::TransferFunctionConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running Transfer Function (.tf) analysis for source {}\n", cfg.InputSource);
            }
            auto res = nodalys::ComputeTransferFunction(parsed.CircuitInstance, cfg, parsed.Options,
                                                        verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::Noise: {
            const auto &cfg = std::get<nodalys::NoiseConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running Noise (.noise) analysis for source {}\n", cfg.InputSource);
            }
            auto res =
                nodalys::ComputeNoise(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::SParameters: {
            const auto &cfg = std::get<nodalys::SParameterConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running S-Parameter (.sp) analysis: {} Hz to {} Hz\n", cfg.StartFrequency,
                                         cfg.StopFrequency);
            }
            auto res = nodalys::ComputeSParameters(parsed.CircuitInstance, cfg, parsed.Options,
                                                   verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::PoleZero: {
            const auto &cfg = std::get<nodalys::PoleZeroConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running Pole-Zero (.pz) analysis: output={}, input={}\n", cfg.OutputNode,
                                         cfg.InputSource);
            }
            auto res =
                nodalys::ComputePoleZero(parsed.CircuitInstance, cfg, parsed.Options, verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }

        case nodalys::AnalysisKind::Sensitivity: {
            const auto &cfg = std::get<nodalys::SensitivityConfig>(analysis.Config);
            if (verbose && !batch_mode) {
                std::cout << std::format("Running DC Sensitivity (.sens) analysis: pos={}, neg={}\n",
                                         cfg.OutputPositiveNode, cfg.OutputNegativeNode);
            }
            auto res = nodalys::ComputeSensitivity(parsed.CircuitInstance, cfg, parsed.Options,
                                                   verbose ? &diagnostics : nullptr);
            if (!res) {
                std::cerr << std::format("Simulation Error: {}\n", res.error());
                return EXIT_FAILURE;
            }
            sim_result = std::move(*res);
            break;
        }
        }

        const auto sim_end = std::chrono::steady_clock::now();
        if (verbose && !batch_mode) {
            const auto sim_ms = std::chrono::duration<double, std::milli>(sim_end - sim_start).count();
            std::cout << std::format("Analysis completed in {:.2f} ms\n", sim_ms);
        }

        // Export result
        std::string exported;
        if (fmt_lower == "csv") {
            exported = nodalys::ExportCsv(sim_result);
        } else if (fmt_lower == "json") {
            exported = nodalys::ExportJson(sim_result);
        } else if (fmt_lower == "touchstone" || fmt_lower == "s2p") {
            if (std::holds_alternative<nodalys::SParameterResult>(sim_result)) {
                exported = nodalys::ExportTouchstone(std::get<nodalys::SParameterResult>(sim_result));
            } else {
                exported = nodalys::ExportCsv(sim_result);
            }
        } else {
            // raw or raw-ascii
            exported = nodalys::ExportRaw(sim_result, sim_title);
        }

        all_exported_output.append(exported);
    }

    if (verbose && !diagnostics.empty()) {
        std::cout << "\nDiagnostic Messages:\n";
        for (const auto &diag : diagnostics) {
            std::cout << "  - " << diag.Text << "\n";
        }
    }

    // Write output
    if (!output_file.empty()) {
        if (!nodalys::SaveStringToFile(output_file, all_exported_output)) {
            std::cerr << std::format("Error: Failed to write output file '{}'\n", output_file);
            return EXIT_FAILURE;
        }
        if (verbose && !batch_mode) {
            std::cout << std::format("Simulation output written to '{}'\n", output_file);
        }
    } else {
        std::cout << all_exported_output;
    }

    return EXIT_SUCCESS;
}

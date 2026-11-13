/**
 * @file    check_dc_sweep.cpp
 * @brief   Checks the settings of a DC sweep before it runs.
 */

#include "simulation/check_dc_sweep.h"

#include "circuit/circuit.h"
#include "circuit/components/source.h"
#include "simulation/analysis_settings.h"
#include "simulation/analysis_sources.h"
#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace Core {

namespace {

/// Most points a DC sweep may take, for the same reason.
constexpr int MAX_DC_SWEEP_POINTS = 100000;

/**
 * @brief   Values from start to stop, both included, as ngspice counts them.
 */
double SweepPointCount(const SweepRange &range) {
    return std::floor((range.Stop - range.Start) / range.Step + 1e-9) + 1.0;
}

std::optional<std::string> CheckSweepRange(const Circuit &circuit, const SweepRange &range, const char *role) {
    if (range.Source.empty()) {
        return std::format("Pick the {} source", role);
    }
    if (FindSweepableSource(circuit, range.Source) == nullptr) {
        return std::format("The {} source {} is not {} of the circuit", role, range.Source, SWEEPABLE_SOURCES);
    }
    if (range.Start == range.Stop) {
        return std::format("The start and stop values of the {} source must differ", role);
    }
    if (range.Step == 0.0 || (range.Stop - range.Start) / range.Step < 0.0) {
        return std::format("The step of the {} source must go from its start value toward its stop value", role);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Checks the sources and ranges of a DC sweep, including the optional stepped source.
 * @param[in] circuit   Circuit to sweep.
 * @param[in] settings  Swept source and range, and the stepped one.
 * @return  The reason the sweep cannot run, such as a source that is not in the circuit or too many points; no value
 * when it can.
 */
std::optional<std::string> CheckDCSweepSettings(const Circuit &circuit, const DCSweepSettings &settings) {
    if (std::optional<std::string> error = CheckSweepRange(circuit, settings.Swept, "swept")) {
        return error;
    }
    double point_count = SweepPointCount(settings.Swept);
    if (settings.Stepped) {
        if (std::optional<std::string> error = CheckSweepRange(circuit, *settings.Stepped, "stepped")) {
            return error;
        }
        if (settings.Stepped->Source == settings.Swept.Source) {
            return "The stepped source must differ from the swept one";
        }
        point_count *= SweepPointCount(*settings.Stepped);
    }
    if (point_count > MAX_DC_SWEEP_POINTS) {
        return std::format("The steps are too small for the ranges: they would produce more than {} points",
                           MAX_DC_SWEEP_POINTS);
    }
    return std::nullopt;
}

} // namespace Core

/**
 * @file    circuit_currents.h
 * @brief   The currents a simulation reports for each part: which terminals are probed and how they are named.
 */

#ifndef IMCSIM_CIRCUIT_CURRENTS_H
#define IMCSIM_CIRCUIT_CURRENTS_H

#include "circuit/circuit.h"
#include "circuit/components/component.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  CurrentProbe
 * @brief   A terminal whose current the simulator measures with a 0 V source in series, positive into the terminal.
 * @details ngspice saves no usable current for diodes and transistors in an AC sweep, so their terminals are
 *          probed in every analysis. Label tells the terminals of one part apart, such as "C" for a collector;
 *          it is empty for a part with a single probe.
 */
struct CurrentProbe {
    int Terminal;
    const char *Label;
};

// Current probes
bool HasBranchCurrent(ComponentType type);
std::vector<CurrentProbe> GetCurrentProbes(const Component &component);
std::string GetCurrentProbeName(const Component &component, std::string_view label);
std::string GetCurrentName(const Component &component, std::string_view label);
bool HasSingleCurrent(ComponentType type);
std::vector<std::string> GetCurrentNames(const Component &component);
std::vector<std::size_t> GetCurrentTerminals(const Component &component);
std::optional<std::string> FindControllingSource(const Circuit &circuit, std::string_view current);

} // namespace Core

#endif // IMCSIM_CIRCUIT_CURRENTS_H

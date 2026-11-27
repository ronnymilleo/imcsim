/**
 * @file    ngspice_vectors.h
 * @brief   Reads vectors from the current plot of ngspice and names the ones the results come from.
 * @details ngGet_Vec_Info fills one static record, so a vector's values are copied before the next one is looked up.
 */

#ifndef IMCSIM_NGSPICE_VECTORS_H
#define IMCSIM_NGSPICE_VECTORS_H

#include "circuit/circuit.h"
#include "circuit/components/component.h"
#include "simulation/simulation_results.h"
#include <complex>
#include <cstddef>
#include <expected>
#include <ngspice/sharedspice.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @struct  CurrentVector
 * @brief   A current the results report, and the ngspice vector it is read from.
 */
struct CurrentVector {
    std::string TraceName;
    std::string VectorName;
};

/**
 * @struct  NodeAndCurrentVectors
 * @brief   The node voltages and component currents a run leaves, a value per point.
 */
struct NodeAndCurrentVectors {
    std::vector<std::vector<double>> NodeVoltages; ///< Indexed by node number; node 0 is ground and always 0 V.
    std::vector<ComponentTrace> Currents;          ///< One per current, in circuit order.
};

// Plots
std::vector<std::string> ListPlotsStartingWith(std::string_view prefix);
[[nodiscard]] std::optional<std::string> FindPlotStartingWith(std::string_view prefix);

// Vectors of the current plot
pvector_info FindVector(std::string name);
std::vector<double> RealPart(const vector_info &vector, std::size_t first_point = 0);
std::vector<std::complex<double>> ComplexValues(const vector_info &vector);
std::vector<std::string> ListVectors(std::string plot);
[[nodiscard]] std::optional<std::vector<double>> ReadRealVector(std::string name);
[[nodiscard]] std::expected<double, std::string> ReadScalar(std::string name, std::string_view what);
[[nodiscard]] std::expected<std::vector<double>, std::string>
ReadRealVectorOfLength(std::string name, std::size_t length, std::string_view what, std::size_t first_point = 0);
[[nodiscard]] std::expected<std::vector<std::complex<double>>, std::string>
ReadComplexVectorOfLength(std::string name, std::size_t length, std::string_view what);
[[nodiscard]] std::expected<NodeAndCurrentVectors, std::string>
ReadNodeAndCurrentVectors(const Circuit &circuit, std::size_t length, std::size_t first_point = 0);

// Names
std::string BranchVectorName(const std::string &element_name);
std::vector<CurrentVector> CurrentVectors(const Component &component);

} // namespace Core

#endif // IMCSIM_NGSPICE_VECTORS_H

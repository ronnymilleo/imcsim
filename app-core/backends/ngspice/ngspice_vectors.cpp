/**
 * @file    ngspice_vectors.cpp
 * @brief   Reads vectors from the current plot of ngspice and names the ones the results come from.
 * @details ngGet_Vec_Info fills one static record, so a vector's values are copied before the next one is looked up.
 */

#include "backends/ngspice/ngspice_vectors.h"

#include "backends/ngspice/ngspice_library.h"
#include "circuit/circuit.h"
#include "circuit/components/source.h"
#include <algorithm>
#include <cctype>
#include <format>

namespace Core {

/**
 * @brief   ngspice keeps no reference to the name after the call.
 */
pvector_info FindVector(std::string name) {
    const pvector_info vector = ngGet_Vec_Info(name.data());
    if (vector == nullptr || vector->v_length < 1 || (vector->v_realdata == nullptr && vector->v_compdata == nullptr)) {
        return nullptr;
    }
    return vector;
}

/**
 * @brief   Analyses with complex results, such as .ac, also store their scale (the frequency) as complex numbers.
 */
std::vector<double> RealPart(const vector_info &vector, const std::size_t first_point) {
    const std::size_t length = static_cast<std::size_t>(vector.v_length);
    std::vector<double> values(length > first_point ? length - first_point : 0);
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t point = first_point + index;
        values[index] = vector.v_realdata != nullptr ? vector.v_realdata[point] : vector.v_compdata[point].cx_real;
    }
    return values;
}

std::vector<std::complex<double>> ComplexValues(const vector_info &vector) {
    std::vector<std::complex<double>> values(static_cast<std::size_t>(vector.v_length));
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = vector.v_compdata != nullptr
                            ? std::complex<double>(vector.v_compdata[index].cx_real, vector.v_compdata[index].cx_imag)
                            : std::complex<double>(vector.v_realdata[index], 0.0);
    }
    return values;
}

/**
 * @brief   ngspice saves the vector names of a plot in lower case; the plot name itself is kept as listed.
 */
std::vector<std::string> ListVectors(std::string plot) {
    std::vector<std::string> names;
    char *const *vectors = ngSpice_AllVecs(plot.data());
    for (; vectors != nullptr && *vectors != nullptr; ++vectors) {
        names.emplace_back(*vectors);
    }
    return names;
}

/**
 * @brief   ngspice stores vector names in lower case.
 */
std::string ToLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::tolower(character); });
    return text;
}

/**
 * @brief   The branch current of a voltage source, an inductor or a current probe.
 */
std::string BranchVectorName(const std::string &element_name) {
    return std::format("{}#branch", ToLower(element_name));
}

/**
 * @brief   Where ngspice saves the currents of a component, with .options savecurrents and the current probes.
 * @details Two-terminal parts report one current, positive from their first terminal to their second; probed parts one
 *          per probed terminal, positive into it.
 */
std::vector<CurrentVector> CurrentVectors(const Component &component) {
    const std::vector<CurrentProbe> probes = GetCurrentProbes(component);
    if (!probes.empty()) {
        std::vector<CurrentVector> vectors;
        for (const CurrentProbe &probe : probes) {
            vectors.push_back({GetCurrentName(component, probe.Label),
                               BranchVectorName(GetCurrentProbeName(component, probe.Label))});
        }
        return vectors;
    }
    const ComponentType type = component.GetType();
    if (!HasSingleCurrent(type)) {
        return {};
    }
    const std::string name = ToLower(component.GetName());
    if (type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR || type == ComponentType::INDUCTOR) {
        return {{component.GetName(), std::format("@{}[i]", name)}};
    }
    if (type == ComponentType::CURRENT_SOURCE) {
        return {{component.GetName(), std::format("@{}[current]", name)}};
    }
    return {{component.GetName(), BranchVectorName(name)}};
}

} // namespace Core

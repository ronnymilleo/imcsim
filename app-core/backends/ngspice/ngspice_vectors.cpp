/**
 * @file    ngspice_vectors.cpp
 * @brief   Reads vectors from the current plot of ngspice and names the ones the results come from.
 * @details ngGet_Vec_Info fills one static record, so a vector's values are copied before the next one is looked up.
 */

#include "backends/ngspice/ngspice_vectors.h"

#include "backends/ngspice/ngspice_library.h"
#include "circuit/circuit.h"
#include "circuit/circuit_currents.h"
#include "circuit/components/source.h"
#include "spice/spice_text.h"
#include <format>
#include <utility>

namespace Core {

/**
 * @brief   Lists the plots ngspice holds whose name starts with a prefix, such as "noise" for the plots of a noise run.
 * @param[in] prefix  Start of the plot names to look for.
 * @return  The names in the order ngspice lists them, none when there is no such plot.
 */
std::vector<std::string> ListPlotsStartingWith(const std::string_view prefix) {
    std::vector<std::string> plots;
    char *const *names = ngSpice_AllPlots();
    for (; names != nullptr && *names != nullptr; ++names) {
        const std::string name(*names);
        if (name.starts_with(prefix)) {
            plots.push_back(name);
        }
    }
    return plots;
}

/**
 * @brief   Finds the plot an analysis leaves, which ngspice names after the analysis, such as "tf" or "sens".
 * @param[in] prefix  Start of the plot name to look for.
 * @return  The name of the first plot that matches, or no value when there is none.
 */
std::optional<std::string> FindPlotStartingWith(const std::string_view prefix) {
    const std::vector<std::string> plots = ListPlotsStartingWith(prefix);
    if (plots.empty()) {
        return std::nullopt;
    }
    return plots.front();
}

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
 * @brief   Reads the real values of a vector, of any length.
 * @details ngspice describes every vector in the same static record, so the values are copied before the next lookup.
 * @param[in] name  Name of the vector.
 * @return  The values, or no value when the vector is missing.
 */
std::optional<std::vector<double>> ReadRealVector(std::string name) {
    const pvector_info vector = FindVector(std::move(name));
    if (vector == nullptr) {
        return std::nullopt;
    }
    return RealPart(*vector);
}

/**
 * @brief   Reads the first value of a vector that holds real numbers, as the analyses with a single point leave.
 * @param[in] name  Name of the vector.
 * @param[in] what  What the vector holds, for the error: "ngspice returned no <what>".
 * @return  The value, or the error when the vector is missing or holds complex numbers.
 */
std::expected<double, std::string> ReadScalar(std::string name, const std::string_view what) {
    const pvector_info vector = FindVector(std::move(name));
    if (vector == nullptr || vector->v_realdata == nullptr) {
        return std::unexpected(std::format("ngspice returned no {}", what));
    }
    return vector->v_realdata[0];
}

/**
 * @brief   Reads a real vector that must have as many points as the scale of the run.
 * @param[in] name         Name of the vector.
 * @param[in] length       Number of points the vector must have.
 * @param[in] what         What the vector holds, for the error: "ngspice returned no <what>".
 * @param[in] first_point  Index of the first point to keep; the ones before it are dropped.
 * @return  The values from first_point on, or the error when the vector is missing or has another length.
 */
std::expected<std::vector<double>, std::string> ReadRealVectorOfLength(std::string name, const std::size_t length,
                                                                       const std::string_view what,
                                                                       const std::size_t first_point) {
    const pvector_info vector = FindVector(std::move(name));
    if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != length) {
        return std::unexpected(std::format("ngspice returned no {}", what));
    }
    return RealPart(*vector, first_point);
}

/**
 * @brief   Reads a vector as complex numbers, which must have as many points as the scale of the run.
 * @param[in] name    Name of the vector.
 * @param[in] length  Number of points the vector must have.
 * @param[in] what    What the vector holds, for the error: "ngspice returned no <what>".
 * @return  The values, or the error when the vector is missing or has another length.
 */
std::expected<std::vector<std::complex<double>>, std::string>
ReadComplexVectorOfLength(std::string name, const std::size_t length, const std::string_view what) {
    const pvector_info vector = FindVector(std::move(name));
    if (vector == nullptr || static_cast<std::size_t>(vector->v_length) != length) {
        return std::unexpected(std::format("ngspice returned no {}", what));
    }
    return ComplexValues(*vector);
}

/**
 * @brief   Reads the voltage of every node and the currents of every component, for the runs that sweep a scale.
 * @param[in] circuit      Circuit that was simulated, whose parts say which vectors to look for.
 * @param[in] length       Number of points of the scale, which every vector must have.
 * @param[in] first_point  Index of the first point to keep; the ones before it are dropped.
 * @return  The voltages and currents, or the error naming the node or current whose vector is missing.
 */
std::expected<NodeAndCurrentVectors, std::string>
ReadNodeAndCurrentVectors(const Circuit &circuit, const std::size_t length, const std::size_t first_point) {
    NodeAndCurrentVectors result;
    result.NodeVoltages.resize(static_cast<std::size_t>(circuit.GetNodeCount()));
    result.NodeVoltages[0].assign(length > first_point ? length - first_point : 0, 0.0);
    for (int node = 1; node < circuit.GetNodeCount(); ++node) {
        auto voltages = ReadRealVectorOfLength(std::format("v({})", node), length,
                                               std::format("voltages for node {}", node), first_point);
        if (!voltages) {
            return std::unexpected(voltages.error());
        }
        result.NodeVoltages[static_cast<std::size_t>(node)] = std::move(*voltages);
    }
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        for (const CurrentVector &current : CurrentVectors(*entry.Part)) {
            auto values = ReadRealVectorOfLength(current.VectorName, length,
                                                 std::format("currents for {}", current.TraceName), first_point);
            if (!values) {
                return std::unexpected(values.error());
            }
            result.Currents.push_back({current.TraceName, std::move(*values)});
        }
    }
    return result;
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

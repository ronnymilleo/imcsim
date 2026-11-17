/**
 * @file    ac_sensitivity.cpp
 * @brief   Turns the figures a solver reports for each element into the AC sensitivity of each part.
 */

#include "simulation/ac_sensitivity.h"

#include "simulation/sensitivity.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <string_view>

namespace Core {

namespace {

std::string ToLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::tolower(character); });
    return text;
}

std::string ToUpper(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::toupper(character); });
    return text;
}

/// The larger the better: how far an element moves the response anywhere in the sweep, in decibels per percent.
double GetPeakEffect(const ACElementFigures &figures) {
    double peak = 0.0;
    for (const std::optional<double> &decibels : figures.MagnitudeDecibelsPerPercent) {
        peak = std::max(peak, std::abs(decibels.value_or(0.0)));
    }
    return peak;
}

ACPartSensitivity MakePart(const Component &part, const ACElementFigures &figures) {
    return {.Name = part.GetName(),
            .Type = part.GetType(),
            .Element = {},
            .Unit = part.GetUnit(),
            .Value = figures.Value,
            .MagnitudeDecibelsPerPercent = figures.MagnitudeDecibelsPerPercent,
            .PhaseDegreesPerPercent = figures.PhaseDegreesPerPercent};
}

/**
 * @brief   Both halves scale together with the total resistance, so their changes per 1 % add up, at the frequencies
 *          where both have a figure.
 */
std::optional<ACPartSensitivity> MakePotentiometerPart(const Component &part, const ACElementFiguresByName &elements) {
    const std::string name = ToLower(part.GetName());
    const auto first = elements.find(std::format("r-{}a", name));
    const auto second = elements.find(std::format("r-{}b", name));
    if (first == elements.end() || second == elements.end()) {
        return std::nullopt;
    }
    const auto add = [](const std::vector<std::optional<double>> &one,
                        const std::vector<std::optional<double>> &other) {
        std::vector<std::optional<double>> sum;
        for (std::size_t index = 0; index < one.size(); ++index) {
            const bool has_both = one[index] && index < other.size() && other[index];
            sum.push_back(has_both ? std::optional(*one[index] + *other[index]) : std::nullopt);
        }
        return sum;
    };
    return MakePart(part, {.Value = part.GetValue(),
                           .MagnitudeDecibelsPerPercent = add(first->second.MagnitudeDecibelsPerPercent,
                                                              second->second.MagnitudeDecibelsPerPercent),
                           .PhaseDegreesPerPercent =
                               add(first->second.PhaseDegreesPerPercent, second->second.PhaseDegreesPerPercent)});
}

const char *InnerUnit(const char type_letter) {
    switch (type_letter) {
    case 'r':
        return "Ohm";
    case 'c':
        return "F";
    case 'l':
        return "H";
    default:
        return "";
    }
}

/**
 * @brief   Whether an element name is a resistor, capacitor or inductor the user placed in the block, rather than an
 *          element imcsim wrote for a part inside it, such as the macromodel of an op-amp ("x-u1") or the halves of a
 *          potentiometer ("r-rv1a"), whose names carry a dash.
 */
bool IsPlacedInside(const std::string &element_name, const std::string &instance) {
    const std::string prefix = instance + ".";
    if (!element_name.starts_with(prefix) || element_name.contains('-')) {
        return false;
    }
    const std::string_view path = std::string_view(element_name).substr(prefix.size());
    const std::size_t last_dot = path.rfind('.');
    const std::string_view leaf = last_dot == std::string_view::npos ? path : path.substr(last_dot + 1);
    return !leaf.empty() && std::string_view("rcl").find(leaf.front()) != std::string_view::npos;
}

/**
 * @brief   The element inside the block that moves the response most, or none when the block holds no resistor,
 *          capacitor or inductor.
 */
std::optional<ACPartSensitivity> MakeBlockPart(const Component &part, const ACElementFiguresByName &elements) {
    const std::string instance = ToLower(part.GetName());
    const ACElementFiguresByName::value_type *largest = nullptr;
    for (const auto &entry : elements) {
        if (!IsPlacedInside(entry.first, instance)) {
            continue;
        }
        if (largest == nullptr || GetPeakEffect(entry.second) > GetPeakEffect(largest->second)) {
            largest = &entry;
        }
    }
    if (largest == nullptr) {
        return std::nullopt;
    }
    ACPartSensitivity result = MakePart(part, largest->second);
    result.Element = ToUpper(largest->first.substr(instance.size() + 1));
    const std::size_t last_dot = result.Element.rfind('.');
    const char type_letter = static_cast<char>(
        std::tolower(static_cast<unsigned char>(result.Element[last_dot == std::string::npos ? 0 : last_dot + 1])));
    result.Unit = InnerUnit(type_letter);
    return result;
}

std::optional<ACPartSensitivity> MakePartSensitivity(const Component &part, const ACElementFiguresByName &elements) {
    const ComponentType type = part.GetType();
    if (type == ComponentType::SUBCIRCUIT) {
        return MakeBlockPart(part, elements);
    }
    if (type == ComponentType::POTENTIOMETER) {
        return MakePotentiometerPart(part, elements);
    }
    if (!HasSensitivity(type)) {
        return std::nullopt;
    }
    const auto found = elements.find(ToLower(part.GetName()));
    if (found == elements.end()) {
        return std::nullopt;
    }
    return MakePart(part, found->second);
}

} // namespace

/**
 * @brief   Works out the AC sensitivity of each part from the figures of the elements.
 * @param[in] circuit   Circuit that was simulated.
 * @param[in] header    The result without its parts: output node, input source, frequencies, output and warnings.
 * @param[in] elements  The figures of every element by lower case name.
 * @return  The header with the parts that have a value, in circuit order: a potentiometer as the sum of its halves and
 *          a block by its element with the largest peak effect. A part the elements do not mention is left out.
 */
ACSensitivity BuildACSensitivity(const Circuit &circuit, ACSensitivity header, const ACElementFiguresByName &elements) {
    header.Parts.clear();
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (std::optional<ACPartSensitivity> part = MakePartSensitivity(*entry.Part, elements)) {
            header.Parts.push_back(std::move(*part));
        }
    }
    return header;
}

} // namespace Core

/**
 * @file    sensitivity.cpp
 * @brief   Turns the sensitivity vectors a solver reports for each element into the sensitivity of each part.
 */

#include "simulation/sensitivity.h"

#include "circuit/components/controlled_source.h"
#include "circuit/components/source.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

namespace {

/// Vector of the change of an element per 100 % change of its value, which ngspice gives resistors, capacitors and
/// inductors.
constexpr std::string_view SCALE_SUFFIX = "_scale";
/// Vector of the change of a controlled source per unit of its gain.
constexpr std::string_view GAIN_SUFFIX = "_gain";

std::string ToLower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::tolower(character); });
    return text;
}

std::string ToUpper(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char character) { return std::toupper(character); });
    return text;
}

std::optional<double> Find(const SensitivityVectors &vectors, const std::string &name) {
    const auto found = vectors.find(name);
    if (found == vectors.end()) {
        return std::nullopt;
    }
    return found->second;
}

/// DC level of a part as written in the netlist; for non-DC independent sources this is the waveform start/offset,
/// not the stored AC/peak magnitude in GetValue().
double EffectiveSensitivityValue(const Component &part) {
    if (!IsSource(part.GetType())) {
        return part.GetValue();
    }
    const auto &source = static_cast<const Source &>(part);
    switch (source.GetSourceType()) {
    case Source::SourceType::DC:
        return source.GetValue();
    case Source::SourceType::AC:
        return source.GetAC().Offset;
    case Source::SourceType::PULSE:
        return source.GetPulse().Low;
    case Source::SourceType::EXP:
        return source.GetEXP().InitialValue;
    case Source::SourceType::SFFM:
        return source.GetSFFM().Offset;
    case Source::SourceType::AM:
        return source.GetAM().Offset;
    case Source::SourceType::PWL:
        return source.GetPWL().Points.empty() ? 0.0 : source.GetPWL().Points.front().Value;
    case Source::SourceType::NOISE:
        return source.GetNoise().Offset;
    case Source::SourceType::RANDOM:
        return source.GetRandom().Second;
    default:
        return source.GetValue();
    }
}

PartSensitivity MakePart(const Component &part, const double absolute, const double normalized) {
    return {.Name = part.GetName(),
            .Type = part.GetType(),
            .Element = {},
            .Unit = part.GetUnit(),
            .Value = EffectiveSensitivityValue(part),
            .Absolute = absolute,
            .Normalized = normalized};
}

/**
 * @brief   A resistor, capacitor or inductor reports both vectors; a source reports its value only, so the change per
 *          100 % is worked out from the value; a part the result has no vector for, such as a capacitor at DC, has
 *          none.
 */
PartSensitivity MakeValuePart(const Component &part, const SensitivityVectors &vectors) {
    const std::string name = ToLower(part.GetName());
    const ComponentType type = part.GetType();
    const std::string vector_name = IsControlledSource(type) ? name + std::string(GAIN_SUFFIX) : name;
    const double absolute = Find(vectors, vector_name).value_or(0.0);
    const std::optional<double> scale = Find(vectors, name + std::string(SCALE_SUFFIX));
    return MakePart(part, absolute, scale.value_or(absolute * EffectiveSensitivityValue(part)));
}

/**
 * @brief   Both halves scale together with the total resistance, so their changes per 100 % add up.
 */
PartSensitivity MakePotentiometerPart(const Component &part, const SensitivityVectors &vectors) {
    const std::string name = ToLower(part.GetName());
    double normalized = 0.0;
    for (const char half : {'a', 'b'}) {
        normalized += Find(vectors, std::format("r-{}{}{}", name, half, SCALE_SUFFIX)).value_or(0.0);
    }
    const double value = part.GetValue();
    return MakePart(part, value > 0.0 ? normalized / value : 0.0, normalized);
}

/**
 * @brief   An element inside a block, found by the vector name ngspice gives it.
 */
struct InnerElement {
    std::string Path;
    std::string Unit;
    double Absolute = 0.0;
    double Normalized = 0.0;
};

std::vector<std::string> SplitPath(const std::string_view text) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('.', start), text.size());
        segments.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return segments;
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
 * @brief   Whether a vector is a resistor, capacitor or inductor the user placed in a block, rather than an element
 *          imcsim wrote for a part inside it, such as the macromodel of an op-amp ("x-u1") or the halves of a
 *          potentiometer ("r-rv1a"), whose names carry a dash.
 */
bool IsPlacedInside(const std::vector<std::string> &segments) {
    const auto has_dash = [](const std::string &segment) { return segment.contains('-'); };
    return segments.size() >= 2 && std::ranges::none_of(segments, has_dash);
}

std::optional<InnerElement> ReadInnerElement(const std::string &vector_name, const std::string &instance,
                                             const SensitivityVectors &vectors) {
    if (vector_name.ends_with(SCALE_SUFFIX) || vector_name.ends_with(GAIN_SUFFIX)) {
        return std::nullopt;
    }
    std::size_t prefix_length = 0;
    char type_letter = '\0';
    // ngspice prefixes subcircuit element vectors with "<type>." (e.g. "r.x1.r1"); Nodalys uses "<instance>.<part>"
    if (vector_name.size() > 2 && vector_name[1] == '.' &&
        std::string_view("rcl").find(vector_name[0]) != std::string_view::npos) {
        prefix_length = 2;
        type_letter = vector_name[0];
    }
    const std::vector<std::string> segments = SplitPath(std::string_view(vector_name).substr(prefix_length));
    const std::optional<double> scale = Find(vectors, vector_name + std::string(SCALE_SUFFIX));
    if (segments.front() != instance || !scale || !IsPlacedInside(segments)) {
        return std::nullopt;
    }
    if (type_letter == '\0') {
        if (segments.back().empty() ||
            std::string_view("rcl").find(segments.back().front()) == std::string_view::npos) {
            return std::nullopt;
        }
        type_letter = segments.back().front();
    }
    const std::string path = ToUpper(vector_name.substr(prefix_length + instance.size() + 1));
    return InnerElement{
        .Path = path, .Unit = InnerUnit(type_letter), .Absolute = vectors.at(vector_name), .Normalized = *scale};
}

/**
 * @brief   The element inside the block with the largest change per 100 %, or none when the block holds no resistor,
 *          capacitor or inductor.
 */
std::optional<PartSensitivity> MakeBlockPart(const Component &part, const SensitivityVectors &vectors) {
    const std::string instance = ToLower(part.GetName());
    std::optional<InnerElement> largest;
    for (const auto &[vector_name, value] : vectors) {
        const std::optional<InnerElement> element = ReadInnerElement(vector_name, instance, vectors);
        if (!element) {
            continue;
        }
        const bool is_better = !largest || std::abs(element->Normalized) > std::abs(largest->Normalized) ||
                               (std::abs(element->Normalized) == std::abs(largest->Normalized) &&
                                element->Unit == "Ohm" && largest->Unit != "Ohm");
        if (is_better) {
            largest = element;
        }
    }
    if (!largest) {
        return std::nullopt;
    }
    // The value is not in the results, but the two vectors give it back: normalized = value x absolute
    std::optional<double> value;
    if (largest->Absolute != 0.0) {
        value = largest->Normalized / largest->Absolute;
    }
    return PartSensitivity{.Name = part.GetName(),
                           .Type = part.GetType(),
                           .Element = largest->Path,
                           .Unit = largest->Unit,
                           .Value = value,
                           .Absolute = largest->Absolute,
                           .Normalized = largest->Normalized};
}

std::optional<PartSensitivity> MakePartSensitivity(const Component &part, const SensitivityVectors &vectors) {
    const ComponentType type = part.GetType();
    if (type == ComponentType::SUBCIRCUIT) {
        return MakeBlockPart(part, vectors);
    }
    if (type == ComponentType::POTENTIOMETER) {
        return MakePotentiometerPart(part, vectors);
    }
    if (HasSensitivity(type)) {
        return MakeValuePart(part, vectors);
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Tells whether a part is one value the sensitivity analysis varies by itself.
 * @param[in] type  Kind of the part.
 * @return  True for resistors, capacitors, inductors, voltage and current sources, supply rails and controlled
 *          sources, whose value is their gain. A potentiometer and a block are listed too, but work from several
 *          elements, so they are not in this set.
 */
bool HasSensitivity(const ComponentType type) {
    return type == ComponentType::RESISTOR || type == ComponentType::CAPACITOR || type == ComponentType::INDUCTOR ||
           type == ComponentType::VOLTAGE_SOURCE || type == ComponentType::VCC ||
           type == ComponentType::CURRENT_SOURCE || IsControlledSource(type);
}

/**
 * @brief   Works out the sensitivity of each part from the vectors of the elements.
 * @param[in] circuit      Circuit that was simulated.
 * @param[in] output_node  Node whose voltage the vectors are the sensitivity of.
 * @param[in] vectors      The finite values of the sensitivity plot by lower case vector name.
 * @return  The parts with a value, a potentiometer as the sum of its halves and a block by its largest element,
 *          the largest absolute normalized sensitivity first. Parts the vectors do not mention count as zero.
 */
Sensitivity BuildSensitivity(const Circuit &circuit, const int output_node, const SensitivityVectors &vectors) {
    Sensitivity result;
    result.OutputNode = output_node;
    for (const CircuitEntry &entry : circuit.GetEntries()) {
        if (std::optional<PartSensitivity> part = MakePartSensitivity(*entry.Part, vectors)) {
            result.Parts.push_back(std::move(*part));
        }
    }
    std::ranges::stable_sort(result.Parts, [](const PartSensitivity &first, const PartSensitivity &second) {
        return std::abs(first.Normalized) > std::abs(second.Normalized);
    });
    return result;
}

} // namespace Core

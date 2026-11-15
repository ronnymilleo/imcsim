/**
 * @file    part_fields.h
 * @brief   Fields shared by the property editors of the parts: one value per member of a parameter set.
 */

#ifndef IMCSIM_PART_FIELDS_H
#define IMCSIM_PART_FIELDS_H

#include "base/value_field.h"
#include <array>
#include <cstddef>
#include <optional>

namespace GUI {

/**
 * @struct  ParameterField
 * @brief   One member of a parameter set, such as Core::PulseParameters, as a field of the part editor.
 */
template <typename Parameters> struct ParameterField {
    const char *Label;
    double Parameters::*Value;
    // No unit means the unit of the source, volts or amperes
    const char *Unit;
    // What the field shows is the stored value times this, such as 100 for a share kept as a fraction and typed in %
    double Scale = 1.0;
};

// Validity
bool AnyValue(double value);

/**
 * @brief   Loads the text of every field from the members of the parameters.
 */
template <typename Parameters, std::size_t Count>
void LoadParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const Parameters &parameters) {
    for (std::size_t index = 0; index < Count; ++index) {
        value_fields[index].Load(parameters.*fields[index].Value * fields[index].Scale);
    }
}

/**
 * @brief   Draws one field per member and applies a valid edit to the parameters; each value is checked by trying it in
 *          a copy of the whole set, since some limits depend on several members.
 * @details Returns whether a value changed.
 */
template <typename Parameters, std::size_t Count>
bool DrawParameterFields(const std::array<ParameterField<Parameters>, Count> &fields,
                         std::array<ValueField, Count> &value_fields, const char *source_unit, const auto &is_valid,
                         Parameters &parameters) {
    bool changed = false;
    for (std::size_t index = 0; index < Count; ++index) {
        const ParameterField<Parameters> &field = fields[index];
        const auto is_valid_value = [&](const double value) {
            Parameters candidate = parameters;
            candidate.*field.Value = value / field.Scale;
            return is_valid(candidate);
        };
        const char *unit = field.Unit != nullptr ? field.Unit : source_unit;
        if (const std::optional<double> value = value_fields[index].Draw(field.Label, unit, is_valid_value)) {
            parameters.*field.Value = *value / field.Scale;
            changed = true;
        }
    }
    return changed;
}

} // namespace GUI

#endif // IMCSIM_PART_FIELDS_H

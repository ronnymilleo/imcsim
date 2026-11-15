/**
 * @file    part_editor_passives.h
 * @brief   The properties of parts with a value: resistors, capacitors, inductors and the like, their initial
 * condition, tolerance and wiper.
 */

#ifndef IMCSIM_PART_EDITOR_PASSIVES_H
#define IMCSIM_PART_EDITOR_PASSIVES_H

#include "base/value_field.h"
#include "circuit/components/component.h"
#include "circuit/components/potentiometer.h"
#include "schematic/preferences.h"
#include "schematic/schematic.h"
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  PassiveFields
 * @brief   The texts being edited for the initial condition and the tolerance of a part.
 */
struct PassiveFields {
    /// Voltage a capacitor or current an inductor starts a transient from; empty when none is set.
    ValueField InitialCondition;
    /// Percent the value may be off in a Monte Carlo run; empty when the part keeps its value.
    ValueField Tolerance;
};

// Loading
void LoadPassiveFields(PassiveFields &fields, const Core::Component &component);

// Drawing
void DrawValue(Core::Component &component, ValueField &main_value, const std::vector<std::string> &block_parameters,
               const Preferences &preferences, Schematic &schematic);
void DrawInitialCondition(Core::Component &component, PassiveFields &fields, Schematic &schematic);
void DrawTolerance(Core::Component &component, PassiveFields &fields, const Preferences &preferences,
                   Schematic &schematic);
void DrawWiper(Core::Potentiometer &pot, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_PASSIVES_H

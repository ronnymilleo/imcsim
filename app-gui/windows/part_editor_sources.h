/**
 * @file    part_editor_sources.h
 * @brief   The properties of voltage and current sources: the waveform they follow and its parameters.
 */

#ifndef IMCSIM_PART_EDITOR_SOURCES_H
#define IMCSIM_PART_EDITOR_SOURCES_H

#include "base/value_field.h"
#include "circuit/components/source.h"
#include "schematic/schematic.h"
#include <array>
#include <string>

namespace GUI {

/**
 * @struct  SourceFields
 * @brief   The texts being edited for the parameters of each waveform of a source.
 * @details One field per member of the parameters of each waveform, in the order the window lists them. The points of
 *          a piecewise linear source are one text of "time value" pairs.
 */
struct SourceFields {
    std::array<ValueField, 4> AC;
    std::array<ValueField, 7> Pulse;
    std::array<ValueField, 6> EXP;
    std::array<ValueField, 5> SFFM;
    std::array<ValueField, 5> AM;
    std::array<ValueField, 8> Noise;
    std::array<ValueField, 4> Random;
    /// The "time value" points of a PWL source being typed, as long as the user wants them.
    std::string PWLText;
};

// Loading
void LoadSourceFields(SourceFields &fields, const Core::Source &source);

// Drawing
void DrawSource(Core::Source &source, SourceFields &fields, ValueField &main_value, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_SOURCES_H

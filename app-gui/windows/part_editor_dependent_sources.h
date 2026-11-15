/**
 * @file    part_editor_dependent_sources.h
 * @brief   The properties of controlled and behavioral sources: their gain, the quantity they follow and their
 * expression.
 */

#ifndef IMCSIM_PART_EDITOR_DEPENDENT_SOURCES_H
#define IMCSIM_PART_EDITOR_DEPENDENT_SOURCES_H

#include "base/value_field.h"
#include "circuit/components/behavioral_source.h"
#include "circuit/components/controlled_source.h"
#include "schematic/schematic.h"
#include <string>

namespace GUI {

// Drawing
void DrawControlledSource(Core::ControlledSource &source, ValueField &main_value, Schematic &schematic);
void DrawBehavioralSource(Core::BehavioralSource &source, std::string &expression_text, Schematic &schematic);

} // namespace GUI

#endif // IMCSIM_PART_EDITOR_DEPENDENT_SOURCES_H

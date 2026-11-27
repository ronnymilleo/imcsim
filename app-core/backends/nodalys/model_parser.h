/**
 * @file    model_parser.h
 * @brief   Turns the .model text of a library model into the parameter structs Nodalys evaluates.
 * @details The ngspice backend writes the model text into the netlist verbatim; Nodalys has no SPICE
 *          parser in its core, so this reads the parameter list of a diode, bipolar transistor or level-1 MOSFET model
 * into the corresponding parameters struct. Parameters Nodalys does not implement are ignored, like the temperature and
 * noise parameters a vendor model carries; a MOSFET model of another level is refused.
 */

#ifndef IMCSIM_NODALYS_MODEL_PARSER_H
#define IMCSIM_NODALYS_MODEL_PARSER_H

#include "circuit/components/bjt.h"
#include "circuit/components/diode.h"
#include "circuit/components/mosfet.h"
#include <expected>
#include <string>

namespace Core {

class Circuit;

// Parameter resolution
[[nodiscard]] std::expected<DiodeParameters, std::string> ResolveParameters(const Circuit &circuit, const Diode &diode);
[[nodiscard]] std::expected<BJTParameters, std::string> ResolveParameters(const Circuit &circuit, const BJT &bjt);
[[nodiscard]] std::expected<MOSFETParameters, std::string> ResolveParameters(const Circuit &circuit,
                                                                             const MOSFET &mosfet);

} // namespace Core

#endif // IMCSIM_NODALYS_MODEL_PARSER_H

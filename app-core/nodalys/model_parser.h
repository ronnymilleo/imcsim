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

#include "components/bjt.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include <expected>
#include <string>

namespace Core {

class Circuit;

/**
 * @brief   The parameters a diode part simulates with: its own, or its library model parsed for Nodalys.
 * @param[in] circuit  Circuit that carries the library model the part names, when it names one.
 * @param[in] diode    Part to resolve the parameters of.
 * @return  The parameters, or the reason its library model cannot be simulated by Nodalys.
 */
std::expected<DiodeParameters, std::string> ResolveParameters(const Circuit &circuit, const Diode &diode);

/// The parameters a BJT part simulates with, like ResolveParameters() for a diode.
std::expected<BJTParameters, std::string> ResolveParameters(const Circuit &circuit, const BJT &bjt);

/// The parameters a MOSFET part simulates with, like ResolveParameters() for a diode.
std::expected<MOSFETParameters, std::string> ResolveParameters(const Circuit &circuit, const MOSFET &mosfet);

} // namespace Core

#endif // IMCSIM_NODALYS_MODEL_PARSER_H

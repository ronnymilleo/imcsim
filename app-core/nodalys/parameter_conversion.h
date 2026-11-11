/**
 * @file    parameter_conversion.h
 * @brief   Turns the parameters of the parts of imcsim into those the Nodalys devices take.
 * @details The parts keep their own structs, with the ratings the results are checked against and the documentation
 *          of each figure, so the model of imcsim does not depend on the engine; only the compiler meets both.
 */

#ifndef IMCSIM_PARAMETER_CONVERSION_H
#define IMCSIM_PARAMETER_CONVERSION_H

#include "components/bjt.h"
#include "components/clock.h"
#include "components/diode.h"
#include "components/mosfet.h"
#include "components/source.h"
#include "components/voltage_controlled_switch.h"
#include <nodalys/types.h>

namespace Core {

// Semiconductors
Nodalys::DiodeParameters ToNodalys(const DiodeParameters &parameters);
Nodalys::BjtParameters ToNodalys(const BJTParameters &parameters);
Nodalys::MosfetParameters ToNodalys(const MOSFETParameters &parameters);

// Waveforms
Nodalys::SineParameters ToNodalys(const ACParameters &parameters);
Nodalys::PulseParameters ToNodalys(const PulseParameters &parameters);
Nodalys::ExpParameters ToNodalys(const EXPParameters &parameters);
Nodalys::PwlParameters ToNodalys(const PWLParameters &parameters);
Nodalys::SffmParameters ToNodalys(const SFFMParameters &parameters);
Nodalys::AmParameters ToNodalys(const AMParameters &parameters);
Nodalys::ClockParameters ToNodalys(const ClockParameters &parameters);

// Switches
Nodalys::SwitchParameters ToNodalys(const SwitchParameters &parameters);

} // namespace Core

#endif // IMCSIM_PARAMETER_CONVERSION_H

/**
 * @file    fuse.cpp
 * @brief   A fuse that blows by I squared t, stays open for the rest of the run and cools between overloads.
 */

#include "components/fuse.h"

#include <array>
#include <memory>

namespace Core {

namespace {

/// Typical figures of glass fuses, not one vendor's datasheet: a fast one melts in about 0.15 s of I^2 t per A^2 of its
/// rating and a slow-blow one in about 1 s.
constexpr auto READY_FUSES = std::to_array<ReadyFuse>({
    {"100 mA fast", {.RatedCurrent = 0.1, .MeltingI2t = 0.0015, .ColdResistance = 4.0, .BlownResistance = 1e9}},
    {"500 mA fast", {.RatedCurrent = 0.5, .MeltingI2t = 0.0375, .ColdResistance = 0.5, .BlownResistance = 1e9}},
    {"1 A fast", {.RatedCurrent = 1.0, .MeltingI2t = 0.15, .ColdResistance = 0.15, .BlownResistance = 1e9}},
    {"5 A fast", {.RatedCurrent = 5.0, .MeltingI2t = 3.75, .ColdResistance = 0.02, .BlownResistance = 1e9}},
    {"1 A slow-blow", {.RatedCurrent = 1.0, .MeltingI2t = 1.0, .ColdResistance = 0.15, .BlownResistance = 1e9}},
});

/// Not a name a user can give a block, so the model never replaces one of theirs.
constexpr const char *FUSE_SUBCIRCUIT_NAME = "imcsim_fuse";

} // namespace

/**
 * @brief   Creates a 1 A fast fuse.
 */
Fuse::Fuse() : Component(ComponentType::FUSE, 0.0) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Fuse::Clone() const {
    return std::make_unique<Fuse>(*this);
}

/**
 * @brief   Returns the figures of the fuse.
 * @return  Rated current, melting I squared t and the two resistances.
 */
const FuseParameters &Fuse::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the figures of the fuse.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void Fuse::SetParameters(const FuseParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of fuse figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the rated current, the melting I squared t and the cold resistance are positive and the blown
 *          resistance is above the cold one.
 */
bool Fuse::IsValidParameters(const FuseParameters &parameters) {
    return parameters.RatedCurrent > 0.0 && parameters.MeltingI2t > 0.0 && parameters.ColdResistance > 0.0 &&
           parameters.BlownResistance > parameters.ColdResistance;
}

/**
 * @brief   Works out how fast the element of a fuse cools.
 * @param[in] parameters  Figures of the fuse, which must be valid.
 * @return  The thermal time constant in seconds. The element heats by the current squared over the rated current
 *          squared, per time constant, and loses its heat in the same time constant, so it settles at the rated
 *          current exactly where it melts, and a short overload melts it at the melting I squared t.
 */
double GetCoolingTimeConstant(const FuseParameters &parameters) {
    return parameters.MeltingI2t / (parameters.RatedCurrent * parameters.RatedCurrent);
}

/**
 * @brief   Lists the fuses with typical figures.
 * @return  Five glass fuses, from 100 mA fast to 1 A slow-blow, valid for the part.
 */
std::span<const ReadyFuse> GetReadyFuses() {
    return READY_FUSES;
}

/**
 * @brief   Returns the name of the subcircuit that simulates a fuse.
 * @return  The name the netlist lines of fuses refer to.
 */
const char *GetFuseSubcircuitName() {
    return FUSE_SUBCIRCUIT_NAME;
}

/**
 * @brief   Writes the subcircuit that simulates a fuse, with the rated current, the cooling time constant and the two
 *          resistances as parameters.
 * @return  The ".subckt" block, newline included.
 * @details The element is a resistance that a behavioral source sets between the cold and the blown values. A
 *          capacitor on the thermal node integrates the current squared over its rated current squared, per time
 *          constant, and a resistor lets the heat leak away in the same time constant, so the node settles at the
 *          current squared over the rated current squared and the fuse is rated to carry what settles at 1 for ever. A
 * latch node charges once the heat passes 1.015, a margin above the 1 that the rated current settles at, and again by
 * itself once it has started, since the open fuse then stops carrying current and the heat falls; nothing discharges
 * it, so the fuse stays open. The latch moves the resistance between its two values through an exponential, so the
 * tails of the steps change it by parts per million. A gate source is 0 in an operating point, a DC sweep and an AC
 * sweep, which therefore see a whole fuse, and rises a nanosecond into a transient, so a fuse is whole at the start of
 * each run. It needs the Gear integration, since the trapezoidal rule rings for ever on an inductor whose current an
 * open fuse cuts.
 */
std::string FormatFuseSubcircuit() {
    return ".subckt imcsim_fuse a b params: Ih=1 Tau=0.15 Rc=0.15 Rb=1e9\n"
           "Vg g 0 DC 0 PULSE(0 1 0 1n 1n 1e30)\n"
           "Rg g 0 1e12\n"
           "Bres rs 0 V = {Rc}*exp(ln({Rb}/{Rc})/(1+exp(-(v(l)-0.5)/0.02)))\n"
           "Rf a b r='v(rs)'\n"
           "Bth 0 t I = v(g)*(v(a,b)/v(rs))**2/({Ih}**2*{Tau})\n"
           "Cth t 0 1\n"
           "Rth t 0 {Tau}\n"
           "Bl 0 l I = v(g)*(v(l) < 1.5 ? 1e4/{Tau}*(1/(1+exp(-(v(t)-1.015)/0.0004))+1/(1+exp(-(v(l)-0.15)/0.002))) : "
           "0)\n"
           "Cl l 0 1\n"
           "Rl l 0 1e9\n"
           ".ends imcsim_fuse\n";
}

} // namespace Core

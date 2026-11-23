/**
 * @file    circuit_builder_sources.cpp
 * @brief   Independent voltage and current source registration methods on CircuitBuilder.
 */

#include "api/circuit_builder_impl.h"
#include "compiler/source_specs.h"
#include "nodalys/circuit_builder.h"
#include <utility>

namespace Nodalys {

/**
 * @brief   Adds an independent DC voltage source with optional AC small-signal excitation.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] dc_volts      DC voltage in Volts.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVdc(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const double dc_volts,
                                       const double ac_mag,
                                       double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeDcSpec(dc_volts, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent DC current source with optional AC small-signal excitation.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node (current flows from `pos` to `neg`).
 * @param[in] neg           Negative terminal node.
 * @param[in] dc_amps       DC current in Amperes.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIdc(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const double dc_amps,
                                       const double ac_mag,
                                       double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeDcSpec(dc_amps, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent pulse voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Pulse waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVpulse(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const PulseParameters &params,
                                          const double ac_mag,
                                          double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakePulseSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent pulse current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Pulse waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIpulse(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const PulseParameters &params,
                                          const double ac_mag,
                                          double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakePulseSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent sinusoidal (or damped sinusoidal) voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Sine waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @param[in] damped        True to apply exponential damping factor `Theta`.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVsine(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SineParameters &params,
                                         const double ac_mag,
                                         double /*ac_phase_deg*/,
                                         const bool damped) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeSineSpec(params, ac_mag, damped));
    return *this;
}

/**
 * @brief   Adds an independent sinusoidal (or damped sinusoidal) current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Sine waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @param[in] damped        True to apply exponential damping factor `Theta`.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIsine(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SineParameters &params,
                                         const double ac_mag,
                                         double /*ac_phase_deg*/,
                                         const bool damped) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeSineSpec(params, ac_mag, damped));
    return *this;
}

/**
 * @brief   Adds an independent exponential voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Exponential waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVexp(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const ExpParameters &params,
                                        const double ac_mag,
                                        double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeExpSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent exponential current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Exponential waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIexp(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const ExpParameters &params,
                                        const double ac_mag,
                                        double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeExpSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent piecewise-linear (PWL) voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Piecewise-linear waveform points and repeat settings.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVpwl(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const PwlParameters &params,
                                        const double ac_mag,
                                        double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakePwlSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent piecewise-linear (PWL) current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        Piecewise-linear waveform points and repeat settings.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIpwl(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const PwlParameters &params,
                                        const double ac_mag,
                                        double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakePwlSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent single-frequency FM (SFFM) voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        SFFM waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVsffm(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SffmParameters &params,
                                         const double ac_mag,
                                         double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeSffmSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent single-frequency FM (SFFM) current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        SFFM waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIsffm(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SffmParameters &params,
                                         const double ac_mag,
                                         double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeSffmSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent amplitude-modulated (AM) voltage source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        AM waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Volts.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVam(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const AmParameters &params,
                                       const double ac_mag,
                                       double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeAmSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent amplitude-modulated (AM) current source.
 * @param[in] name          Element designator.
 * @param[in] pos           Positive terminal node.
 * @param[in] neg           Negative terminal node.
 * @param[in] params        AM waveform parameters.
 * @param[in] ac_mag        AC small-signal magnitude in Amperes.
 * @param[in] ac_phase_deg  Reserved AC small-signal phase in degrees.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIam(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const AmParameters &params,
                                       const double ac_mag,
                                       double /*ac_phase_deg*/) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeAmSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent transient noise voltage source (`TRNOISE`).
 * @param[in] name    Element designator.
 * @param[in] pos     Positive terminal node.
 * @param[in] neg     Negative terminal node.
 * @param[in] params  Transient noise waveform parameters.
 * @param[in] ac_mag  AC small-signal magnitude in Volts.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVnoise(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const NoiseParameters &params,
                                          const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeNoiseSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent transient noise current source (`TRNOISE`).
 * @param[in] name    Element designator.
 * @param[in] pos     Positive terminal node.
 * @param[in] neg     Negative terminal node.
 * @param[in] params  Transient noise waveform parameters.
 * @param[in] ac_mag  AC small-signal magnitude in Amperes.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddInoise(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const NoiseParameters &params,
                                          const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeNoiseSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent random-sample voltage source (`TRRANDOM`).
 * @param[in] name    Element designator.
 * @param[in] pos     Positive terminal node.
 * @param[in] neg     Negative terminal node.
 * @param[in] params  Random waveform distribution and timing parameters.
 * @param[in] ac_mag  AC small-signal magnitude in Volts.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddVrandom(std::string name,
                                           const NodeId pos,
                                           const NodeId neg,
                                           const RandomParameters &params,
                                           const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeRandomSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds an independent random-sample current source (`TRRANDOM`).
 * @param[in] name    Element designator.
 * @param[in] pos     Positive terminal node.
 * @param[in] neg     Negative terminal node.
 * @param[in] params  Random waveform distribution and timing parameters.
 * @param[in] ac_mag  AC small-signal magnitude in Amperes.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddIrandom(std::string name,
                                           const NodeId pos,
                                           const NodeId neg,
                                           const RandomParameters &params,
                                           const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeRandomSpec(params, ac_mag));
    return *this;
}

/**
 * @brief   Adds a periodic digital clock voltage source.
 * @param[in] name    Element designator.
 * @param[in] pos     Positive terminal node.
 * @param[in] neg     Negative terminal node.
 * @param[in] params  Clock frequency, duty cycle, voltage levels, and edge times.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &
CircuitBuilder::AddClock(std::string name, const NodeId pos, const NodeId neg, const ClockParameters &params) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeClockSpec(params));
    return *this;
}

/**
 * @brief   Adds a constant logic-level voltage source.
 * @param[in] name        Element designator.
 * @param[in] pos         Positive terminal node.
 * @param[in] neg         Negative terminal node.
 * @param[in] high_state  True to drive `high_volts`, false to drive `low_volts`.
 * @param[in] high_volts  Logic-high voltage in Volts.
 * @param[in] low_volts   Logic-low voltage in Volts.
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddLogicInput(std::string name,
                                              const NodeId pos,
                                              const NodeId neg,
                                              const bool high_state,
                                              const double high_volts,
                                              const double low_volts) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeDcSpec(high_state ? high_volts : low_volts, 0.0));
    return *this;
}

/**
 * @brief   Adds a 0 V independent voltage source used as an ideal current sense probe for CCCS/CCVS.
 * @param[in] probe_name  Probe designator referenced by controlled sources.
 * @param[in] node_in     Positive terminal node (current enters here).
 * @param[in] node_out    Negative terminal node (current exits here).
 * @return  Reference to this builder for chaining.
 */
CircuitBuilder &CircuitBuilder::AddCurrentProbe(std::string probe_name, const NodeId node_in, const NodeId node_out) {
    m_Impl->AddSource(std::move(probe_name), node_in, node_out, true, MakeDcSpec(0.0, 0.0));
    return *this;
}

} // namespace Nodalys

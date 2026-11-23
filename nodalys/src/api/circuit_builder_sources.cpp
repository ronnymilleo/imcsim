/**
 * @file    circuit_builder_sources.cpp
 * @brief   Independent voltage and current source registration methods on CircuitBuilder.
 */

#include "api/circuit_builder_impl.h"
#include "compiler/source_specs.h"
#include "nodalys/circuit_builder.h"
#include <utility>

namespace Nodalys {

CircuitBuilder &CircuitBuilder::AddVdc(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const double dc_volts,
                                       const double ac_mag,
                                       double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeDcSpec(dc_volts, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIdc(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const double dc_amps,
                                       const double ac_mag,
                                       double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeDcSpec(dc_amps, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVpulse(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const PulseParameters &params,
                                          const double ac_mag,
                                          double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakePulseSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIpulse(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const PulseParameters &params,
                                          const double ac_mag,
                                          double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakePulseSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVsine(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SineParameters &params,
                                         const double ac_mag,
                                         double,
                                         const bool damped) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeSineSpec(params, ac_mag, damped));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIsine(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SineParameters &params,
                                         const double ac_mag,
                                         double,
                                         const bool damped) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeSineSpec(params, ac_mag, damped));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVexp(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const ExpParameters &params,
                                        const double ac_mag,
                                        double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeExpSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIexp(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const ExpParameters &params,
                                        const double ac_mag,
                                        double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeExpSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVpwl(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const PwlParameters &params,
                                        const double ac_mag,
                                        double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakePwlSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIpwl(std::string name,
                                        const NodeId pos,
                                        const NodeId neg,
                                        const PwlParameters &params,
                                        const double ac_mag,
                                        double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakePwlSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVsffm(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SffmParameters &params,
                                         const double ac_mag,
                                         double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeSffmSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIsffm(std::string name,
                                         const NodeId pos,
                                         const NodeId neg,
                                         const SffmParameters &params,
                                         const double ac_mag,
                                         double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeSffmSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVam(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const AmParameters &params,
                                       const double ac_mag,
                                       double) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeAmSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIam(std::string name,
                                       const NodeId pos,
                                       const NodeId neg,
                                       const AmParameters &params,
                                       const double ac_mag,
                                       double) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeAmSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVnoise(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const NoiseParameters &params,
                                          const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeNoiseSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddInoise(std::string name,
                                          const NodeId pos,
                                          const NodeId neg,
                                          const NoiseParameters &params,
                                          const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeNoiseSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddVrandom(std::string name,
                                           const NodeId pos,
                                           const NodeId neg,
                                           const RandomParameters &params,
                                           const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeRandomSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddIrandom(std::string name,
                                           const NodeId pos,
                                           const NodeId neg,
                                           const RandomParameters &params,
                                           const double ac_mag) {
    m_Impl->AddSource(std::move(name), pos, neg, false, MakeRandomSpec(params, ac_mag));
    return *this;
}

CircuitBuilder &
CircuitBuilder::AddClock(std::string name, const NodeId pos, const NodeId neg, const ClockParameters &params) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeClockSpec(params));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddLogicInput(std::string name,
                                              const NodeId pos,
                                              const NodeId neg,
                                              const bool high_state,
                                              const double high_volts,
                                              const double low_volts) {
    m_Impl->AddSource(std::move(name), pos, neg, true, MakeDcSpec(high_state ? high_volts : low_volts, 0.0));
    return *this;
}

CircuitBuilder &CircuitBuilder::AddCurrentProbe(std::string probe_name, const NodeId node_in, const NodeId node_out) {
    m_Impl->AddSource(std::move(probe_name), node_in, node_out, true, MakeDcSpec(0.0, 0.0));
    return *this;
}

} // namespace Nodalys

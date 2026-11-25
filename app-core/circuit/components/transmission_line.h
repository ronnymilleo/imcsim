/**
 * @file    transmission_line.h
 * @brief   A uniform two-port TEM transmission line in the simulation model, lossless or with skin and dielectric loss.
 */

#ifndef IMCSIM_TRANSMISSION_LINE_H
#define IMCSIM_TRANSMISSION_LINE_H

#include "circuit/components/component.h"
#include <memory>

namespace Core {

/// Characteristic impedance a newly placed transmission line starts with, in Ohms.
constexpr double DEFAULT_TRANSMISSION_LINE_OHMS = 50.0;

/**
 * @struct  TransmissionLineParameters
 * @brief   Delay and optional conductor/dielectric loss figures of a two-port transmission line.
 * @details The component value holds the lossless characteristic impedance \f$Z_0\f$ in Ohms, so total inductance is
 *          \f$L = Z_0 T_d\f$ and total capacitance is \f$C = T_d / Z_0\f$. When all four loss figures are zero, the
 *          line is lossless and runs in both time-domain (Bergeron) and frequency-domain analyses. Any positive loss
 *          adds \f$R(f) = R_{\text{DC}} + R_{\text{skin}}\sqrt{f / f_{\text{ref}}}\f$ and
 *          \f$G(f) = G_{\text{DC}} + 2\pi f C \tan\delta\f$ in frequency-domain and Harmonic Balance analyses, while
 *          transient runs refuse the part explicitly.
 */
struct TransmissionLineParameters {
    double Delay = 1e-9;
    double DcResistance = 0.0;
    double SkinResistance = 0.0;
    double DcConductance = 0.0;
    double LossTangent = 0.0;
    double ReferenceFrequency = 1e9;

    bool IsLossy() const;
    bool operator==(const TransmissionLineParameters &) const = default;
};

/**
 * @class   TransmissionLine
 * @brief   Four-terminal two-port transmission line: port 1 terminals 1 (+) and 2 (-), then port 2 terminals 3 (+) and
 *          4 (-).
 */
class TransmissionLine : public Component {
public:
    TransmissionLine();
    ~TransmissionLine() override = default;

    std::unique_ptr<Component> Clone() const override;

    const TransmissionLineParameters &GetParameters() const;
    void SetParameters(const TransmissionLineParameters &parameters);
    static bool IsValidParameters(const TransmissionLineParameters &parameters);
    double GetQuarterWaveFrequency() const;

private:
    TransmissionLineParameters m_Parameters;
};

} // namespace Core

#endif // IMCSIM_TRANSMISSION_LINE_H

/**
 * @file    transformer.h
 * @brief   A transformer in the simulation model: two coupled inductors.
 */

#ifndef IMCSIM_TRANSFORMER_H
#define IMCSIM_TRANSFORMER_H

#include "component.h"

namespace Core {

/**
 * @struct  TransformerParameters
 * @brief   What sets how a transformer steps a voltage, and how well it does it.
 * @details The turns ratio is primary turns over secondary turns, so 2 steps a voltage down to half. The coupling
 *          is the share of the flux of one winding that goes through the other, below 1 for a real core. The
 *          winding resistance is that of each winding, the loss of its copper.
 */
struct TransformerParameters {
    double TurnsRatio = 1.0;
    double Coupling = 0.999;
    double WindingResistance = 0.1;
};

/**
 * @class   Transformer
 * @brief   Two windings on one core: primary terminals 1 (+) and 2 (-), then secondary terminals 3 (+) and 4 (-).
 * @details The value is the inductance of the primary; the secondary's follows from the turns ratio, as the square of
 *          the turns, so a 2:1 transformer of 100 mH has a secondary of 25 mH. The + terminals are the dotted ones:
 *          a voltage rising on the primary + makes the secondary + rise too.
 */
class Transformer : public Component {
public:
    Transformer();
    ~Transformer() override = default;

    const TransformerParameters &GetParameters() const;
    void SetParameters(const TransformerParameters &parameters);
    static bool IsValidParameters(const TransformerParameters &parameters);
    double GetSecondaryInductance() const;

private:
    TransformerParameters m_Parameters;
};

} // namespace Core

#endif // IMCSIM_TRANSFORMER_H

/**
 * @file    logic_part.h
 * @brief   Logic gates and flip-flops in the simulation model.
 */

#ifndef IMCSIM_LOGIC_PART_H
#define IMCSIM_LOGIC_PART_H

#include "component.h"
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  LogicParameters
 * @brief   The logic family of a gate or flip-flop.
 * @details A voltage at or above 70% of the logic level reads as 1 and at or below 30% as 0, in between is
 *          undefined. The outputs swing between 0 V and the logic level.
 */
struct LogicParameters {
    double LogicLevel = 5.0;
    double PropagationDelay = 10e-9;
};

/**
 * @class   LogicPart
 * @brief   A logic gate or a flip-flop, which ngspice runs as digital code models behind analog bridges.
 * @details Terminals are the inputs in the order GetLogicInputLabels() gives, then the outputs. The part has no
 *          value and no supply pin: its logic level is a figure of the part. An unconnected input reads as 0.
 */
class LogicPart : public Component {
public:
    explicit LogicPart(ComponentType type);
    ~LogicPart() override = default;

    const LogicParameters &GetParameters() const;
    void SetParameters(const LogicParameters &parameters);
    static bool IsValidParameters(const LogicParameters &parameters);

private:
    LogicParameters m_Parameters;
};

bool IsLogicGate(ComponentType type);
bool IsFlipFlop(ComponentType type);
bool IsLogicPart(ComponentType type);
std::vector<std::string> GetLogicInputLabels(ComponentType type);
std::vector<std::string> GetLogicOutputLabels(ComponentType type);
const char *GetLogicModelKind(ComponentType type);

} // namespace Core

#endif // IMCSIM_LOGIC_PART_H

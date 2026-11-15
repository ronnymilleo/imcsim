/**
 * @file    logic_part.h
 * @brief   Logic gates and flip-flops in the simulation model.
 */

#ifndef IMCSIM_LOGIC_PART_H
#define IMCSIM_LOGIC_PART_H

#include "circuit/components/component.h"
#include <memory>
#include <string>
#include <vector>

namespace Core {

/// Highest voltage that reads as 0, as a fraction of the logic level.
inline constexpr double LOGIC_LOW_FRACTION = 0.3;
/// Lowest voltage that reads as 1, as a fraction of the logic level; between the two a voltage is unknown.
inline constexpr double LOGIC_HIGH_FRACTION = 0.7;

/// Fewest inputs a gate with several inputs takes.
inline constexpr int MIN_LOGIC_INPUTS = 2;
/// Most inputs a gate with several inputs takes.
inline constexpr int MAX_LOGIC_INPUTS = 8;

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
 * @details Terminals are the inputs in the order GetInputLabels() gives, then the outputs. The part has no
 *          value and no supply pin: its logic level is a figure of the part. An unconnected input reads as 0. AND, OR,
 *          NAND, NOR, XOR and XNOR gates take from 2 to 8 inputs; an XOR is high for an odd number of high inputs and
 *          an XNOR for an even number. The count is not a figure of the logic family, so it stands apart from
 *          LogicParameters.
 */
class LogicPart : public Component {
public:
    explicit LogicPart(ComponentType type);
    ~LogicPart() override = default;

    std::unique_ptr<Component> Clone() const override;

    const LogicParameters &GetParameters() const;
    void SetParameters(const LogicParameters &parameters);
    static bool IsValidParameters(const LogicParameters &parameters);

    int GetInputCount() const;
    void SetInputCount(int count);
    static bool IsValidInputCount(int count);
    std::vector<std::string> GetInputLabels() const;

private:
    LogicParameters m_Parameters;
    int m_InputCount = MIN_LOGIC_INPUTS;
};

bool IsLogicGate(ComponentType type);
bool IsFlipFlop(ComponentType type);
bool IsLogicPart(ComponentType type);
bool HasVariableInputs(ComponentType type);
std::vector<std::string> GetLogicInputLabels(ComponentType type);
std::vector<std::string> GetLogicInputLabels(ComponentType type, int input_count);
std::vector<std::string> GetLogicOutputLabels(ComponentType type);
const char *GetLogicModelKind(ComponentType type);

} // namespace Core

#endif // IMCSIM_LOGIC_PART_H

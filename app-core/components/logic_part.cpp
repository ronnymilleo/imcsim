/**
 * @file    logic_part.cpp
 * @brief   Logic gates and flip-flops in the simulation model.
 */

#include "logic_part.h"

namespace Core {

/**
 * @brief   Creates a gate or flip-flop with a 5 V logic level and a 10 ns propagation delay.
 * @param[in] type  A type for which IsLogicPart() is true.
 */
LogicPart::LogicPart(const ComponentType type) : Component(type, 0.0) {
}

/**
 * @brief   Returns the logic family of the part.
 * @return  Logic level and propagation delay.
 */
const LogicParameters &LogicPart::GetParameters() const {
    return m_Parameters;
}

/**
 * @brief   Sets the logic family of the part.
 * @param[in] parameters  New figures; check them with IsValidParameters() first.
 */
void LogicPart::SetParameters(const LogicParameters &parameters) {
    m_Parameters = parameters;
}

/**
 * @brief   Checks whether a set of logic figures can be simulated.
 * @param[in] parameters  Candidate figures.
 * @return  True when the logic level is positive and the delay is not negative.
 */
bool LogicPart::IsValidParameters(const LogicParameters &parameters) {
    return parameters.LogicLevel > 0.0 && parameters.PropagationDelay >= 0.0;
}

/**
 * @brief   Returns how many inputs the part has.
 * @return  The chosen count for a gate that takes several inputs, and the fixed count of any other part.
 */
int LogicPart::GetInputCount() const {
    return HasVariableInputs(m_Type) ? m_InputCount : static_cast<int>(GetLogicInputLabels(m_Type).size());
}

/**
 * @brief   Sets how many inputs a gate has.
 * @param[in] count  New count; check it with IsValidInputCount() first.
 * @note    Parts other than the AND, OR, NAND, NOR, XOR and XNOR gates keep their inputs, so the call does nothing.
 */
void LogicPart::SetInputCount(const int count) {
    if (HasVariableInputs(m_Type)) {
        m_InputCount = count;
    }
}

/**
 * @brief   Checks whether a gate can have a number of inputs.
 * @param[in] count  Candidate count.
 * @return  True from 2 to 8.
 */
bool LogicPart::IsValidInputCount(const int count) {
    return count >= MinLogicInputs && count <= MaxLogicInputs;
}

/**
 * @brief   Names the input terminals of this part, in terminal order.
 * @return  What GetLogicInputLabels() gives for the type and the input count of the part.
 */
std::vector<std::string> LogicPart::GetInputLabels() const {
    return GetLogicInputLabels(m_Type, GetInputCount());
}

/**
 * @brief   Tells whether a kind of component is a logic gate.
 * @param[in] type  Kind of component.
 * @return  True for AND, OR, NOT, NAND, NOR, XOR, XNOR and the buffer.
 */
bool IsLogicGate(const ComponentType type) {
    switch (type) {
    case ComponentType::And:
    case ComponentType::Or:
    case ComponentType::Not:
    case ComponentType::Nand:
    case ComponentType::Nor:
    case ComponentType::Xor:
    case ComponentType::Xnor:
    case ComponentType::Buffer:
        return true;
    default:
        return false;
    }
}

/**
 * @brief   Tells whether a kind of component is a flip-flop.
 * @param[in] type  Kind of component.
 * @return  True for the D, JK, T and SR flip-flops.
 */
bool IsFlipFlop(const ComponentType type) {
    return type == ComponentType::DFlipFlop || type == ComponentType::JKFlipFlop || type == ComponentType::TFlipFlop ||
           type == ComponentType::SRFlipFlop;
}

/**
 * @brief   Tells whether a kind of component is a LogicPart, so it can be cast to one.
 * @param[in] type  Kind of component.
 * @return  True for the gates and the flip-flops.
 */
bool IsLogicPart(const ComponentType type) {
    return IsLogicGate(type) || IsFlipFlop(type);
}

/**
 * @brief   Tells whether a gate can have more than two inputs.
 * @param[in] type  Kind of component.
 * @return  True for AND, OR, NAND, NOR, XOR and XNOR.
 */
bool HasVariableInputs(const ComponentType type) {
    return type == ComponentType::And || type == ComponentType::Or || type == ComponentType::Nand ||
           type == ComponentType::Nor || type == ComponentType::Xor || type == ComponentType::Xnor;
}

/**
 * @brief   Names the input terminals of a gate or flip-flop with its usual number of inputs, in terminal order.
 * @param[in] type  A type for which IsLogicPart() is true.
 * @return  "A" and "B" for a gate, or "A" alone for NOT and the buffer; for a flip-flop its data inputs, the clock
 *          "CLK", then the asynchronous "SET" and "RESET", which are active high.
 */
std::vector<std::string> GetLogicInputLabels(const ComponentType type) {
    return GetLogicInputLabels(type, MinLogicInputs);
}

/**
 * @brief   Names the input terminals of a gate or flip-flop, in terminal order.
 * @param[in] type         A type for which IsLogicPart() is true.
 * @param[in] input_count  Number of inputs of a gate that takes several; the other parts ignore it.
 * @return  "A", "B", "C"... for a gate that takes several inputs, "A" alone for NOT and the buffer; for a flip-flop
 *          its data inputs, the clock "CLK", then the asynchronous "SET" and "RESET", which are active high.
 */
std::vector<std::string> GetLogicInputLabels(const ComponentType type, const int input_count) {
    if (HasVariableInputs(type)) {
        std::vector<std::string> labels;
        for (int input = 0; input < input_count; ++input) {
            labels.emplace_back(1, static_cast<char>('A' + input));
        }
        return labels;
    }
    switch (type) {
    case ComponentType::Not:
    case ComponentType::Buffer:
        return {"A"};
    case ComponentType::DFlipFlop:
        return {"D", "CLK", "SET", "RESET"};
    case ComponentType::JKFlipFlop:
        return {"J", "K", "CLK", "SET", "RESET"};
    case ComponentType::TFlipFlop:
        return {"T", "CLK", "SET", "RESET"};
    case ComponentType::SRFlipFlop:
        return {"S", "R", "CLK", "SET", "RESET"};
    default:
        return {"A", "B"};
    }
}

/**
 * @brief   Names the output terminals of a gate or flip-flop, which follow the inputs.
 * @param[in] type  A type for which IsLogicPart() is true.
 * @return  "Y" for a gate; "Q" and "QN" for a flip-flop.
 */
std::vector<std::string> GetLogicOutputLabels(const ComponentType type) {
    if (IsFlipFlop(type)) {
        return {"Q", "QN"};
    }
    return {"Y"};
}

/**
 * @brief   Returns the ngspice digital code model that runs a kind of gate or flip-flop.
 * @param[in] type  A type for which IsLogicPart() is true.
 * @return  The model name, such as "d_and" or "d_dff".
 */
const char *GetLogicModelKind(const ComponentType type) {
    switch (type) {
    case ComponentType::And:
        return "d_and";
    case ComponentType::Or:
        return "d_or";
    case ComponentType::Not:
        return "d_inverter";
    case ComponentType::Nand:
        return "d_nand";
    case ComponentType::Nor:
        return "d_nor";
    case ComponentType::Xor:
        return "d_xor";
    case ComponentType::Xnor:
        return "d_xnor";
    case ComponentType::Buffer:
        return "d_buffer";
    case ComponentType::DFlipFlop:
        return "d_dff";
    case ComponentType::JKFlipFlop:
        return "d_jkff";
    case ComponentType::TFlipFlop:
        return "d_tff";
    case ComponentType::SRFlipFlop:
        return "d_srff";
    default:
        return "";
    }
}

} // namespace Core

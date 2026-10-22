/**
 * @file    math_channels.h
 * @brief   Traces computed from others, like the math channels of an oscilloscope: their list, editor and evaluation.
 */

#ifndef IMCSIM_MATH_CHANNELS_H
#define IMCSIM_MATH_CHANNELS_H

#include "implot.h"
#include "simulator.h"
#include "trace_math.h"
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @struct  MathChannel
 * @brief   A trace computed from others, like the math channel of an oscilloscope.
 * @details Either an operation between two traces, picked with buttons, or a typed expression; both compile to
 *          the same program. Channels belong to the Output window for now and are not saved with the schematic.
 */
struct MathChannel {
    std::string Name;
    bool Shown = true;
    bool UsesExpression = false;
    std::string First;
    MathOperator Operator = MathOperator::SUBTRACT;
    std::string Second;
    std::string Expression;
    // Compiled from the operation or the expression, whichever is in use
    std::expected<MathProgram, std::string> Program = std::unexpected("Not compiled yet");
};

/**
 * @struct  MathTrace
 * @brief   A math channel evaluated over one result, ready to plot and measure.
 */
struct MathTrace {
    std::string Label;
    std::vector<double> Values;
    std::string Unit;
    ImAxis Axis = ImAxis_Y1;
    ImU32 Color = 0;
};

/**
 * @class   MathChannels
 * @brief   The math channels of the Output window, numbered M1, M2... as they are added, each with its own color.
 */
class MathChannels {
public:
    // Panel beside the plots
    void Draw(const std::vector<std::string> &operands);

    // Evaluation
    bool IsEmpty() const;
    std::vector<MathTrace> Evaluate(std::span<const double> xs, Dimension x_unit,
                                    const std::vector<std::vector<double>> &node_voltages,
                                    const std::vector<Core::ComponentTrace> &currents,
                                    std::vector<std::string> &errors) const;

private:
    std::vector<MathChannel> m_Channels;
    int m_NextNumber = 1;

    bool DrawEditor(MathChannel &channel, const std::vector<std::string> &operands);
};

bool UsesAxis(const std::vector<MathTrace> &math, ImAxis axis);
std::string ThirdAxisUnit(const std::vector<MathTrace> &math);
std::vector<std::string> ListMathLabels(const std::vector<MathTrace> &math);

} // namespace GUI

#endif // IMCSIM_MATH_CHANNELS_H

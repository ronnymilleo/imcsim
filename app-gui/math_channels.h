/**
 * @file    math_channels.h
 * @brief   Traces computed from others, like the math channels of an oscilloscope: their list, editor and evaluation.
 */

#ifndef IMCSIM_MATH_CHANNELS_H
#define IMCSIM_MATH_CHANNELS_H

#include "implot.h"
#include "math_channel.h"
#include "schematic.h"
#include "simulator.h"
#include "trace_math.h"
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace GUI {

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
 * @brief   The panel and evaluation of the math channels, numbered M1, M2... as they are added, each with its own
 * color.
 * @details The channels belong to the schematic, which saves them with the file; their net names are read on the
 *          circuit as it is now.
 */
class MathChannels {
public:
    explicit MathChannels(Schematic &schematic);

    // Panel beside the plots
    void Draw(const std::vector<std::string> &operands);

    // Evaluation
    bool IsEmpty() const;
    std::string GetSignature() const;
    std::vector<MathTrace> Evaluate(std::span<const double> xs, Dimension x_unit,
                                    const std::vector<std::vector<double>> &node_voltages,
                                    const std::vector<Core::ComponentTrace> &currents,
                                    std::vector<std::string> &errors) const;

private:
    Schematic &m_Schematic;

    NetResolver GetNetResolver() const;
    bool DrawEditor(MathChannel &channel, const std::vector<std::string> &operands) const;
};

bool UsesAxis(const std::vector<MathTrace> &math, ImAxis axis);
std::string ThirdAxisUnit(const std::vector<MathTrace> &math);
std::vector<std::string> ListMathLabels(const std::vector<MathTrace> &math);

} // namespace GUI

#endif // IMCSIM_MATH_CHANNELS_H

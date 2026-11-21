/**
 * @file    trace_math_eval.cpp
 * @brief   Evaluates compiled math channels over the samples of a result, or over the phasors of an AC sweep.
 */

#include "results/trace_math_eval.h"

#include "results/trace_math_samples.h"
#include <algorithm>
#include <complex>
#include <format>
#include <functional>
#include <utility>

namespace GUI {

namespace {

using Kind = MathInstruction::Kind;

/**
 * @struct  StackEntry
 * @brief   A value on the evaluation stack: a trace or a number spread over every sample, and its unit.
 */
template <typename Value>
struct StackEntry {
    std::vector<Value> Values;
    Dimension Unit;
    bool IsNumber = false;
};

/**
 * @enum    OperandCount
 * @brief   How many entries of the stack an instruction takes: none for a load, one for a function, two for an
 * operator.
 */
enum class OperandCount {
    NONE,
    ONE,
    TWO
};

OperandCount GetOperandCount(const Kind type) {
    switch (type) {
    case Kind::NUMBER:
    case Kind::VOLTAGE:
    case Kind::CURRENT:
        return OperandCount::NONE;
    case Kind::NEGATE:
    case Kind::ABS:
    case Kind::SQUARE_ROOT:
    case Kind::DERIVATIVE:
    case Kind::INTEGRAL:
        return OperandCount::ONE;
    case Kind::ADD:
    case Kind::SUBTRACT:
    case Kind::MULTIPLY:
    case Kind::DIVIDE:
    default:
        return OperandCount::TWO;
    }
}

template <typename Value>
StackEntry<Value> PopEntry(std::vector<StackEntry<Value>> &stack) {
    StackEntry<Value> entry = std::move(stack.back());
    stack.pop_back();
    return entry;
}

/**
 * @brief   Reads what a load instruction puts on the stack: a number, the voltage of a node or the current of a part.
 */
template <typename Samples>
std::expected<StackEntry<typename Samples::Value>, std::string> LoadOperand(const MathInstruction &instruction,
                                                                            const Samples &samples) {
    using Value = typename Samples::Value;
    switch (instruction.Type) {
    case Kind::NUMBER:
        return StackEntry<Value>{std::vector<Value>(samples.GetCount(), Value(instruction.Number)), {}, true};
    case Kind::VOLTAGE: {
        if (!instruction.Net.empty()) {
            return std::unexpected(std::format("The net {} was not resolved to a node", instruction.Net));
        }
        auto voltage = samples.GetVoltage(instruction.Node);
        if (!voltage) {
            return std::unexpected(voltage.error());
        }
        return StackEntry<Value>{std::move(*voltage), {.Volts = 1}, false};
    }
    case Kind::CURRENT:
    default: {
        auto current = samples.GetCurrent(instruction.Current);
        if (!current) {
            return std::unexpected(current.error());
        }
        return StackEntry<Value>{std::move(*current), {.Amperes = 1}, false};
    }
    }
}

/**
 * @brief   Applies a function to the entry on top of the stack, with the unit that comes out of it.
 */
template <typename Samples>
StackEntry<typename Samples::Value>
ApplyUnary(const Kind type, StackEntry<typename Samples::Value> operand, const Samples &samples) {
    using Value = typename Samples::Value;
    switch (type) {
    case Kind::NEGATE:
        std::ranges::transform(operand.Values, operand.Values.begin(), [](const Value value) { return -value; });
        break;
    case Kind::ABS:
        std::ranges::transform(operand.Values, operand.Values.begin(), [](const Value value) {
            return Value(std::abs(value));
        });
        break;
    case Kind::SQUARE_ROOT:
        std::ranges::transform(operand.Values, operand.Values.begin(), [](const Value value) {
            return std::sqrt(value);
        });
        operand.Unit = GetSquareRootUnit(operand.Unit);
        break;
    case Kind::DERIVATIVE:
        operand.Values = samples.GetDerivative(operand.Values);
        operand.Unit = DivideUnits(operand.Unit, samples.GetXUnit());
        operand.IsNumber = false;
        break;
    case Kind::INTEGRAL:
        operand.Values = samples.GetIntegral(operand.Values);
        operand.Unit = MultiplyUnits(operand.Unit, samples.GetXUnit());
        operand.IsNumber = false;
        break;
    default:
        break;
    }
    return operand;
}

template <typename Value>
Dimension GetBinaryUnit(const Kind type, const StackEntry<Value> &first, const StackEntry<Value> &second) {
    switch (type) {
    case Kind::ADD:
    case Kind::SUBTRACT:
        return AddUnits(first.Unit, first.IsNumber, second.Unit, second.IsNumber);
    case Kind::MULTIPLY:
        return MultiplyUnits(first.Unit, second.Unit);
    case Kind::DIVIDE:
    default:
        return DivideUnits(first.Unit, second.Unit);
    }
}

template <typename Value, typename Operation>
StackEntry<Value> CombineSamples(StackEntry<Value> first,
                                 const StackEntry<Value> &second,
                                 const Operation &operation,
                                 const Dimension unit) {
    for (std::size_t index = 0; index < first.Values.size(); ++index) {
        first.Values[index] = operation(first.Values[index], second.Values[index]);
    }
    first.Unit = unit;
    first.IsNumber = first.IsNumber && second.IsNumber;
    return first;
}

/**
 * @brief   Applies an operator to the two entries on top of the stack, sample by sample, with the unit that comes out
 * of it.
 */
template <typename Value>
StackEntry<Value> ApplyBinaryInstruction(const Kind type, StackEntry<Value> first, const StackEntry<Value> &second) {
    // The unit is read before the first entry is moved into the result
    const Dimension unit = GetBinaryUnit(type, first, second);
    switch (type) {
    case Kind::ADD:
        return CombineSamples(std::move(first), second, std::plus<>(), unit);
    case Kind::SUBTRACT:
        return CombineSamples(std::move(first), second, std::minus<>(), unit);
    case Kind::MULTIPLY:
        return CombineSamples(std::move(first), second, std::multiplies<>(), unit);
    case Kind::DIVIDE:
    default:
        return CombineSamples(std::move(first), second, std::divides<>(), unit);
    }
}

/**
 * @brief   Runs a compiled expression on a stack of traces, for real samples or for phasors alike.
 * @details Every instruction but the loads takes its operands from the stack, which the parser always fills.
 */
template <typename Samples>
std::expected<StackEntry<typename Samples::Value>, std::string> RunProgram(const MathProgram &program,
                                                                           const Samples &samples) {
    using Entry = StackEntry<typename Samples::Value>;
    std::vector<Entry> stack;
    for (const MathInstruction &instruction : program.Instructions) {
        switch (GetOperandCount(instruction.Type)) {
        case OperandCount::NONE: {
            auto loaded = LoadOperand(instruction, samples);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            stack.push_back(std::move(*loaded));
            break;
        }
        case OperandCount::ONE:
            stack.push_back(ApplyUnary(instruction.Type, PopEntry(stack), samples));
            break;
        case OperandCount::TWO:
        default: {
            const Entry second = PopEntry(stack);
            Entry first = PopEntry(stack);
            stack.push_back(ApplyBinaryInstruction(instruction.Type, std::move(first), second));
            break;
        }
        }
    }
    return std::move(stack.back());
}

ACMathResult ToACMathResult(const StackEntry<std::complex<double>> &entry) {
    ACMathResult result{.MagnitudesDecibels = {}, .PhasesDegrees = {}, .Unit = entry.Unit};
    for (const std::complex<double> value : entry.Values) {
        const PhasorFigures figures = ToDecibelsAndDegrees(value);
        result.MagnitudesDecibels.push_back(figures.MagnitudeDecibels);
        result.PhasesDegrees.push_back(figures.PhaseDegrees);
    }
    return result;
}

} // namespace

/**
 * @brief   Evaluates a compiled expression over every sample of a result.
 * @param[in] program        Compiled expression, with its net names resolved.
 * @param[in] xs             X value of each sample: times, or the values of a swept source.
 * @param[in] x_unit         Unit of the X values, which derivatives divide by and integrals multiply by.
 * @param[in] node_voltages  Voltage of each node at each sample, indexed by node number.
 * @param[in] currents       Currents of the result, found by name.
 * @return  One value per sample and their unit, or an error naming a node or part the result does not have.
 * @note    Dividing by zero gives infinite or NaN values, which plots skip.
 */
std::expected<MathResult, std::string> EvaluateExpression(const MathProgram &program,
                                                          const std::span<const double> xs,
                                                          const Dimension x_unit,
                                                          const std::vector<std::vector<double>> &node_voltages,
                                                          const std::vector<Core::ComponentTrace> &currents) {
    auto result = RunProgram(program, RealSamples(xs, x_unit, node_voltages, currents));
    if (!result) {
        return std::unexpected(result.error());
    }
    return MathResult{.Values = std::move(result->Values), .Unit = result->Unit};
}

/**
 * @brief   Evaluates a compiled expression over an AC sweep, on the phasors of its traces.
 * @details V(2)/V(1) so divides the complex values, which gives the gain and the phase shift between two nodes, and
 *          ddt() and integ() multiply and divide by \f$ j\omega \f$. The result reads as a magnitude in decibels,
 *          relative to 1 of its unit (\f$ 20\log_{10} \f$, as for every trace of an AC sweep), and a phase in degrees.
 * @param[in] program  Compiled expression, with its net names resolved.
 * @param[in] sweep    The AC sweep.
 * @return  The magnitude and phase at each frequency and their unit, or an error naming a node or part the sweep does
 *          not have. A magnitude of zero or one that is not finite, such as integ() at 0 Hz, gives NaN in both.
 */
std::expected<ACMathResult, std::string> EvaluateACExpression(const MathProgram &program, const Core::ACSweep &sweep) {
    const auto result = RunProgram(program, PhasorSamples(sweep));
    if (!result) {
        return std::unexpected(result.error());
    }
    return ToACMathResult(*result);
}

} // namespace GUI

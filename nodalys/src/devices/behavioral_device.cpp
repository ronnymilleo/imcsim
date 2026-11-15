/**
 * @file    behavioral_device.cpp
 * @brief   Behavioral sources of Nodalys: a voltage or a current that follows an expression of node voltages, branch
 *          currents and time.
 */

#include "devices/behavioral_device.h"

#include "devices/semiconductor_math.h"
#include <format>
#include <utility>

namespace Nodalys {

/**
 * @brief   Tells which branch carries the current an input reads, once every source of the circuit exists.
 * @param[in] input   Index of the input in the order of the expression's inputs.
 * @param[in] branch  Unknown of the branch of the source it names.
 */
void BehavioralSourceDevice::BindCurrent(const std::size_t input, const int branch) {
    m_Bindings[input].Unknown = branch;
}

/**
 * @brief   Tells that the device is nonlinear: its expression is evaluated again at every iterate.
 */
bool BehavioralSourceDevice::IsLinear() const {
    return false;
}

/**
 * @brief   Tells whether the harmonic balance can run the device.
 * @return  True unless the expression reads `time`: the balance evaluates a nonlinear device at each sample of the
 *          state without the instant of the sample.
 */
bool BehavioralSourceDevice::SupportsHarmonicBalance() const {
    return !m_Expression.IsTimeDependent();
}

std::string_view BehavioralSourceDevice::GetFault() const {
    return m_Fault;
}

/**
 * @brief   Updates the temperature that `temper` reads inside the expression.
 * @param[in] thermal  Temperature of the run.
 */
void BehavioralSourceDevice::SetTemperature(const ThermalConditions &thermal) {
    m_TemperatureCelsius = thermal.TemperatureKelvin - CELSIUS_TO_KELVIN;
}

BehavioralSourceDevice::BehavioralSourceDevice(std::string name, Expression expression,
                                               std::vector<BehavioralBinding> bindings)
    : m_Name(std::move(name)), m_Expression(std::move(expression)), m_Bindings(std::move(bindings)),
      m_Partials(m_Bindings.size(), 0.0) {
}

/**
 * @brief   Evaluates the expression at the iterate and keeps its value, its derivatives and the constant of the
 *          linearization.
 * @param[in] x     Current iterate.
 * @param[in] time  What the stamp is for: `time` reads the instant of a transient point and 0 otherwise.
 */
void BehavioralSourceDevice::Linearize(const std::vector<double> &x, const TimeContext &time) {
    m_TimeSeconds = time.IsTransient ? time.Time : 0.0;
    const std::vector<double> inputs = ReadInputs(x);
    const std::expected<ExpressionResult, std::string> result =
        m_Expression.Evaluate(inputs, m_TimeSeconds, m_TemperatureCelsius);
    if (result) {
        m_Fault.clear();
        m_Value = result->Value;
        m_Partials = result->Partials;
    } else {
        m_Fault = std::format("{}: {}", m_Name, result.error());
        m_Value = 0.0;
        m_Partials.assign(m_Bindings.size(), 0.0);
    }
    m_Offset = m_Value;
    for (std::size_t input = 0; input < inputs.size(); ++input) {
        m_Offset -= m_Partials[input] * inputs[input];
    }
}

/**
 * @brief   Adds the derivatives of the expression to a row of the matrix, each multiplying the unknown it reads.
 * @param[in,out] sys   System to stamp into.
 * @param[in]     row   Row to stamp: a node for a current, the branch for a voltage.
 * @param[in]     sign  +1 to add the linearization to the row, -1 to subtract it.
 */
void BehavioralSourceDevice::StampPartials(MnaSystem &sys, const int row, const double sign) const {
    for (std::size_t input = 0; input < m_Bindings.size(); ++input) {
        const BehavioralBinding &binding = m_Bindings[input];
        const double partial = sign * m_Partials[input];
        sys.AddA(row, binding.Unknown, partial);
        if (!binding.IsCurrent) {
            sys.AddA(row, binding.Reference, -partial);
        }
    }
}

/**
 * @brief   Evaluates the expression at a solution, at the instant of the last stamp.
 * @param[in] x  Solution to read the inputs from.
 * @return  The value, or 0 where the expression has none, which the fault of the stamp reports.
 */
double BehavioralSourceDevice::EvaluateAt(const std::vector<double> &x) const {
    const std::expected<ExpressionResult, std::string> result =
        m_Expression.Evaluate(ReadInputs(x), m_TimeSeconds, m_TemperatureCelsius);
    return result ? result->Value : 0.0;
}

/**
 * @brief   Gives the small-signal value of the expression: the sum of each derivative at the operating point times
 *          the phasor of its input.
 * @param[in] x_ac  Small-signal solution.
 */
std::complex<double> BehavioralSourceDevice::SmallSignalValue(const std::vector<std::complex<double>> &x_ac) const {
    std::complex<double> value;
    for (std::size_t input = 0; input < m_Bindings.size(); ++input) {
        const BehavioralBinding &binding = m_Bindings[input];
        const std::complex<double> phasor =
            binding.IsCurrent ? x_ac[static_cast<std::size_t>(binding.Unknown)]
                              : AcNodeVoltage(x_ac, binding.Unknown) - AcNodeVoltage(x_ac, binding.Reference);
        value += m_Partials[input] * phasor;
    }
    return value;
}

double BehavioralSourceDevice::GetOffset() const {
    return m_Offset;
}

std::vector<double> BehavioralSourceDevice::ReadInputs(const std::vector<double> &x) const {
    std::vector<double> values;
    values.reserve(m_Bindings.size());
    for (const BehavioralBinding &binding : m_Bindings) {
        if (binding.IsCurrent) {
            values.push_back(x[static_cast<std::size_t>(binding.Unknown)]);
        } else {
            values.push_back(NodeVoltage(x, binding.Unknown) - NodeVoltage(x, binding.Reference));
        }
    }
    return values;
}

BehavioralVoltageDevice::BehavioralVoltageDevice(std::string name, const int node_a, const int node_b, const int branch,
                                                 Expression expression, std::vector<BehavioralBinding> bindings)
    : BehavioralSourceDevice(std::move(name), std::move(expression), std::move(bindings)), m_NodeA(node_a),
      m_NodeB(node_b), m_Branch(branch) {
}

int BehavioralVoltageDevice::GetBranch() const {
    return m_Branch;
}

/**
 * @brief   Stamps the branch equation v(a) - v(b) - F = 0 linearized at the iterate.
 */
void BehavioralVoltageDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    Linearize(x, time);
    sys.AddA(m_NodeA, m_Branch, 1.0);
    sys.AddA(m_NodeB, m_Branch, -1.0);
    sys.AddA(m_Branch, m_NodeA, 1.0);
    sys.AddA(m_Branch, m_NodeB, -1.0);
    StampPartials(sys, m_Branch, -1.0);
    sys.AddRhs(m_Branch, GetOffset());
}

double BehavioralVoltageDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = x[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

std::complex<double> BehavioralVoltageDevice::AcTerminalCurrent(const int terminal,
                                                                const std::vector<double> & /*x_dc*/,
                                                                const std::vector<std::complex<double>> &x_ac,
                                                                const double /*omega*/) const {
    const std::complex<double> current = x_ac[static_cast<std::size_t>(m_Branch)];
    return terminal == 0 ? current : -current;
}

BehavioralCurrentDevice::BehavioralCurrentDevice(std::string name, const int node_a, const int node_b,
                                                 Expression expression, std::vector<BehavioralBinding> bindings)
    : BehavioralSourceDevice(std::move(name), std::move(expression), std::move(bindings)), m_NodeA(node_a),
      m_NodeB(node_b) {
}

/**
 * @brief   Stamps the current F leaving node a and entering node b, linearized at the iterate.
 */
void BehavioralCurrentDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    Linearize(x, time);
    StampPartials(sys, m_NodeA, 1.0);
    StampPartials(sys, m_NodeB, -1.0);
    StampCurrentSource(sys, m_NodeA, m_NodeB, GetOffset());
}

double BehavioralCurrentDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double current = EvaluateAt(x);
    return terminal == 0 ? current : -current;
}

std::complex<double> BehavioralCurrentDevice::AcTerminalCurrent(const int terminal,
                                                                const std::vector<double> & /*x_dc*/,
                                                                const std::vector<std::complex<double>> &x_ac,
                                                                const double /*omega*/) const {
    const std::complex<double> current = SmallSignalValue(x_ac);
    return terminal == 0 ? current : -current;
}

} // namespace Nodalys

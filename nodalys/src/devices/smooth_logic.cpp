/**
 * @file    smooth_logic.cpp
 * @brief   Smooth continuous C^inf logic gate macromodels with inertial propagation delay and analog output stages.
 */

#include "devices/smooth_logic.h"

#include <algorithm>
#include <cmath>

namespace nodalys {

namespace {

void StampConductance(MnaSystem &sys, const int node_a, const int node_b, const double g) {
    if (node_a >= 0) {
        sys.AddA(node_a, node_a, g);
    }
    if (node_b >= 0) {
        sys.AddA(node_b, node_b, g);
    }
    if (node_a >= 0 && node_b >= 0) {
        sys.AddA(node_a, node_b, -g);
        sys.AddA(node_b, node_a, -g);
    }
}

} // namespace

/**
 * @brief   Constructs a smooth continuous logic gate.
 * @param[in] type      Kind of logic function (AND, OR, NOT, etc.).
 * @param[in] output    MNA unknown index of the output node.
 * @param[in] inputs    MNA unknown indices of the input nodes.
 * @param[in] internal  MNA unknown index of the internal delay node (-1 for zero delay).
 * @param[in] params    Physical logic thresholds, delay, and output resistance.
 */
SmoothLogicDevice::SmoothLogicDevice(const LogicGateType type, const int output, std::vector<int> inputs,
                                     const int internal, SmoothLogicParameters params)
    : m_Type(type), m_Output(output), m_Inputs(std::move(inputs)), m_Internal(internal), m_Params(std::move(params)) {
    if (m_Params.PropagationDelay > 0.0 && m_Internal >= 0) {
        constexpr double LN2 = 0.6931471805599453;
        m_FilterCapacitance = (m_Params.PropagationDelay / LN2) * m_FilterConductance;
    }
}

/**
 * @brief   Stamps the linearized Newton-Raphson companion model and dynamic state into the MNA system.
 * @param[in,out] sys   MNA system to stamp into.
 * @param[in]     x     Current Newton iterate.
 * @param[in]     time  Transient time context.
 */
void SmoothLogicDevice::Stamp(MnaSystem &sys, const std::vector<double> &x, const TimeContext &time) {
    // High-impedance pull-down leakage conductances to prevent floating nodes
    const double g_in = 1.0 / std::max(m_Params.InputResistance, 1.0);
    for (const int u_in : m_Inputs) {
        if (u_in >= 0) {
            sys.AddA(u_in, u_in, g_in);
        }
    }

    const LogicEvaluation eval = Evaluate(x);

    if (m_Internal >= 0 && m_FilterCapacitance > 0.0) {
        // Internal low-pass RC inertial delay stage
        if (time.IsTransient) {
            m_CapConductance =
                time.BackwardEuler ? m_FilterCapacitance / time.TimeStep : 2.0 * m_FilterCapacitance / time.TimeStep;
            m_CapEquivalent = -m_CapConductance * m_Voltage - (time.BackwardEuler ? 0.0 : m_Current);
            sys.AddA(m_Internal, m_Internal, m_CapConductance);
            sys.AddRhs(m_Internal, -m_CapEquivalent);
        } else {
            m_CapConductance = 0.0;
            m_CapEquivalent = 0.0;
        }

        const double g_drv = m_FilterConductance;
        sys.AddA(m_Internal, m_Internal, g_drv);

        double sum_lin = 0.0;
        for (std::size_t i = 0; i < m_Inputs.size(); ++i) {
            const int u_in = m_Inputs[i];
            const double deriv = eval.Derivatives[i];
            if (u_in >= 0) {
                sys.AddA(m_Internal, u_in, -g_drv * deriv);
                sum_lin += deriv * x[static_cast<std::size_t>(u_in)];
            }
        }
        if (eval.OutputDerivative != 0.0 && m_Output >= 0) {
            sys.AddA(m_Internal, m_Output, -g_drv * eval.OutputDerivative);
            sum_lin += eval.OutputDerivative * x[static_cast<std::size_t>(m_Output)];
        }
        sys.AddRhs(m_Internal, g_drv * (eval.TargetVoltage - sum_lin));

        // Thevenin output stage resistance between internal node and output pin
        const double g_out = 1.0 / std::max(m_Params.OutputResistance, 1e-6);
        StampConductance(sys, m_Internal, m_Output, g_out);
    } else {
        // Direct output drive with finite output resistance
        const double g_out = 1.0 / std::max(m_Params.OutputResistance, 1e-6);
        if (m_Output >= 0) {
            sys.AddA(m_Output, m_Output, g_out);
            double sum_lin = 0.0;
            for (std::size_t i = 0; i < m_Inputs.size(); ++i) {
                const int u_in = m_Inputs[i];
                const double deriv = eval.Derivatives[i];
                if (u_in >= 0) {
                    sys.AddA(m_Output, u_in, -g_out * deriv);
                    sum_lin += deriv * x[static_cast<std::size_t>(u_in)];
                }
            }
            if (eval.OutputDerivative != 0.0) {
                sys.AddA(m_Output, m_Output, -g_out * eval.OutputDerivative);
                sum_lin += eval.OutputDerivative * x[static_cast<std::size_t>(m_Output)];
            }
            sys.AddRhs(m_Output, g_out * (eval.TargetVoltage - sum_lin));
        }
    }
}

/**
 * @brief   Stamps small-signal reactive capacitance coefficients into the AC system.
 * @param[in,out] reactive  System holding imaginary-part coefficients.
 * @param[in]     x_dc      Converged DC operating point.
 */
void SmoothLogicDevice::StampAc(MnaSystem &reactive, const std::vector<double> & /*x_dc*/) const {
    if (m_Internal >= 0 && m_FilterCapacitance > 0.0) {
        reactive.AddA(m_Internal, m_Internal, m_FilterCapacitance);
    }
}

void SmoothLogicDevice::StampCharge(MnaSystem &sys, const std::vector<double> & /*x*/) const {
    if (m_Internal >= 0 && m_FilterCapacitance > 0.0) {
        sys.AddA(m_Internal, m_Internal, m_FilterCapacitance);
    }
}

/**
 * @brief   Limits voltage changes at the gate's output and delay nodes to ensure Newton-Raphson convergence.
 * @param[in,out] x_new  Candidate solution vector to limit.
 * @param[in]     x_old  Previous iterate solution vector.
 * @return  True if any node voltage was limited.
 */
bool SmoothLogicDevice::Limit(std::vector<double> &x_new, const std::vector<double> &x_old) const {
    bool limited = false;
    const double span = std::abs(m_Params.VHigh - m_Params.VLow);
    const double max_step = 0.35 * std::max(span, 1.0);
    const auto limit_node = [&](const int node) {
        if (node < 0 || static_cast<std::size_t>(node) >= x_new.size()) {
            return;
        }
        const double delta = x_new[static_cast<std::size_t>(node)] - x_old[static_cast<std::size_t>(node)];
        if (std::abs(delta) > max_step) {
            x_new[static_cast<std::size_t>(node)] =
                x_old[static_cast<std::size_t>(node)] + std::copysign(max_step, delta);
            limited = true;
        }
    };
    limit_node(m_Internal);
    limit_node(m_Output);
    return limited;
}

/**
 * @brief   Calculates current entering the given device terminal.
 * @param[in] terminal  Terminal index (0 = output, 1..N = inputs).
 * @param[in] x         Current solution vector.
 * @return  Current in Amperes entering the terminal.
 */
double SmoothLogicDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    if (terminal == 0) {
        const double v_out = ReadVoltage(x, m_Output);
        const double v_drv = (m_Internal >= 0) ? ReadVoltage(x, m_Internal) : Evaluate(x).TargetVoltage;
        return (v_out - v_drv) / std::max(m_Params.OutputResistance, 1e-6);
    }
    if (terminal >= 1 && terminal <= static_cast<int>(m_Inputs.size())) {
        const double v_in = ReadVoltage(x, m_Inputs[static_cast<std::size_t>(terminal - 1)]);
        return v_in / std::max(m_Params.InputResistance, 1.0);
    }
    return 0.0;
}

/**
 * @brief   Calculates small-signal AC current entering the given terminal.
 * @param[in] terminal  Terminal index.
 * @param[in] x_dc      DC operating point.
 * @param[in] x_ac      Complex AC solution vector.
 * @param[in] omega     Angular frequency.
 * @return  Complex current phasor entering the terminal.
 */
std::complex<double> SmoothLogicDevice::AcTerminalCurrent(const int terminal, const std::vector<double> & /*x_dc*/,
                                                          const std::vector<std::complex<double>> &x_ac,
                                                          const double /*omega*/) const {
    if (terminal == 0) {
        const std::complex<double> v_out = (m_Output >= 0) ? x_ac[static_cast<std::size_t>(m_Output)] : 0.0;
        const std::complex<double> v_drv = (m_Internal >= 0) ? x_ac[static_cast<std::size_t>(m_Internal)] : 0.0;
        return (v_out - v_drv) / std::max(m_Params.OutputResistance, 1e-6);
    }
    if (terminal >= 1 && terminal <= static_cast<int>(m_Inputs.size())) {
        const int u = m_Inputs[static_cast<std::size_t>(terminal - 1)];
        return (u >= 0) ? (x_ac[static_cast<std::size_t>(u)] / std::max(m_Params.InputResistance, 1.0)) : 0.0;
    }
    return {0.0, 0.0};
}

/**
 * @brief   Returns whether this gate carries transient state between time steps.
 * @return  True when internal inertial delay capacitance is active.
 */
bool SmoothLogicDevice::HasTransientState() const {
    return m_Internal >= 0 && m_FilterCapacitance > 0.0;
}

/**
 * @brief   Commits the accepted transient step values.
 * @param[in] x  Converged transient solution vector.
 */
void SmoothLogicDevice::AcceptStep(const std::vector<double> &x) {
    if (HasTransientState()) {
        const double v_int = ReadVoltage(x, m_Internal);
        m_Current = m_CapConductance * v_int + m_CapEquivalent;
        m_Voltage = v_int;
        m_SavedVoltage = m_Voltage;
        m_SavedCurrent = m_Current;
    }
}

/**
 * @brief   Restores previous state upon local truncation error step rejection.
 */
void SmoothLogicDevice::RejectStep() {
    if (HasTransientState()) {
        m_Voltage = m_SavedVoltage;
        m_Current = m_SavedCurrent;
    }
}

/**
 * @brief   Evaluates the smooth logic function and exact analytical derivatives.
 * @param[in] x  Current iterate vector.
 * @return  Evaluation containing target voltage and partial derivatives.
 */
SmoothLogicDevice::LogicEvaluation SmoothLogicDevice::Evaluate(const std::vector<double> &x) const {
    LogicEvaluation eval;
    eval.Derivatives.resize(m_Inputs.size(), 0.0);

    const std::size_t input_count = m_Inputs.size();
    std::vector<double> s(input_count, 0.0);
    std::vector<double> s_deriv(input_count, 0.0);

    double v_th = m_Params.VThreshold;
    double d_vth_dvout = 0.0;

    if (m_Type == LogicGateType::SCHMITT_TRIGGER) {
        const double v_out = ReadVoltage(x, m_Output);
        const double v_mid = 0.5 * (m_Params.VHigh + m_Params.VLow);
        const double v_span = m_Params.VHigh - m_Params.VLow;
        if (std::abs(v_span) > 1e-9) {
            v_th -= (m_Params.Hysteresis / v_span) * (v_out - v_mid);
            d_vth_dvout = -m_Params.Hysteresis / v_span;
        }
    }

    const double delta = std::max(m_Params.TransitionWidth, 1e-6);
    for (std::size_t i = 0; i < input_count; ++i) {
        const double v_in = ReadVoltage(x, m_Inputs[i]);
        const double arg = std::clamp((v_in - v_th) / delta, -40.0, 40.0);
        const double th = std::tanh(arg);
        s[i] = 0.5 * (1.0 + th);
        s_deriv[i] = 0.5 * (1.0 - th * th) / delta;
    }

    double logic_level = 0.0;
    switch (m_Type) {
    case LogicGateType::NOT: {
        if (!s.empty()) {
            logic_level = 1.0 - s[0];
            eval.Derivatives[0] = -s_deriv[0];
        }
        break;
    }
    case LogicGateType::BUFFER: {
        if (!s.empty()) {
            logic_level = s[0];
            eval.Derivatives[0] = s_deriv[0];
        }
        break;
    }
    case LogicGateType::SCHMITT_TRIGGER: {
        if (!s.empty()) {
            logic_level = s[0];
            eval.Derivatives[0] = s_deriv[0];
            eval.OutputDerivative = -s_deriv[0] * d_vth_dvout;
        }
        break;
    }
    case LogicGateType::AND: {
        logic_level = 1.0;
        for (const double val : s) {
            logic_level *= val;
        }
        for (std::size_t k = 0; k < input_count; ++k) {
            double prod = s_deriv[k];
            for (std::size_t j = 0; j < input_count; ++j) {
                if (j != k) {
                    prod *= s[j];
                }
            }
            eval.Derivatives[k] = prod;
        }
        break;
    }
    case LogicGateType::NAND: {
        double and_level = 1.0;
        for (const double val : s) {
            and_level *= val;
        }
        logic_level = 1.0 - and_level;
        for (std::size_t k = 0; k < input_count; ++k) {
            double prod = s_deriv[k];
            for (std::size_t j = 0; j < input_count; ++j) {
                if (j != k) {
                    prod *= s[j];
                }
            }
            eval.Derivatives[k] = -prod;
        }
        break;
    }
    case LogicGateType::OR: {
        double nor_level = 1.0;
        for (const double val : s) {
            nor_level *= (1.0 - val);
        }
        logic_level = 1.0 - nor_level;
        for (std::size_t k = 0; k < input_count; ++k) {
            double prod = s_deriv[k];
            for (std::size_t j = 0; j < input_count; ++j) {
                if (j != k) {
                    prod *= (1.0 - s[j]);
                }
            }
            eval.Derivatives[k] = prod;
        }
        break;
    }
    case LogicGateType::NOR: {
        logic_level = 1.0;
        for (const double val : s) {
            logic_level *= (1.0 - val);
        }
        for (std::size_t k = 0; k < input_count; ++k) {
            double prod = s_deriv[k];
            for (std::size_t j = 0; j < input_count; ++j) {
                if (j != k) {
                    prod *= (1.0 - s[j]);
                }
            }
            eval.Derivatives[k] = -prod;
        }
        break;
    }
    case LogicGateType::XOR: {
        if (input_count >= 2) {
            const double s0 = s[0];
            const double s1 = s[1];
            logic_level = s0 + s1 - 2.0 * s0 * s1;
            eval.Derivatives[0] = s_deriv[0] * (1.0 - 2.0 * s1);
            eval.Derivatives[1] = s_deriv[1] * (1.0 - 2.0 * s0);
        }
        break;
    }
    case LogicGateType::XNOR: {
        if (input_count >= 2) {
            const double s0 = s[0];
            const double s1 = s[1];
            logic_level = 1.0 - (s0 + s1 - 2.0 * s0 * s1);
            eval.Derivatives[0] = -s_deriv[0] * (1.0 - 2.0 * s1);
            eval.Derivatives[1] = -s_deriv[1] * (1.0 - 2.0 * s0);
        }
        break;
    }
    }

    const double v_span = m_Params.VHigh - m_Params.VLow;
    eval.TargetVoltage = m_Params.VLow + v_span * logic_level;
    for (double &deriv : eval.Derivatives) {
        deriv *= v_span;
    }
    eval.OutputDerivative *= v_span;

    return eval;
}

/**
 * @brief   Reads node voltage from iterate vector, returning 0.0 for ground or inactive nodes.
 * @param[in] x     Iterate vector.
 * @param[in] node  Unknown node index.
 * @return  Voltage in Volts.
 */
double SmoothLogicDevice::ReadVoltage(const std::vector<double> &x, const int node) const {
    if (node < 0 || static_cast<std::size_t>(node) >= x.size()) {
        return 0.0;
    }
    return x[static_cast<std::size_t>(node)];
}

} // namespace nodalys

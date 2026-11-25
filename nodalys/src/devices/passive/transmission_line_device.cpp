/**
 * @file    transmission_line_device.cpp
 * @brief   Uniform two-port TEM transmission line: Bergeron delay line in transient and dispersive telegrapher
 * equations in frequency-domain and Harmonic Balance analyses.
 */

#include "devices/passive/transmission_line_device.h"

#include "devices/stamp_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>

namespace Nodalys {

namespace {

/// Threshold below which \f$(1 - e^{-\theta}) / \theta\f$ is evaluated via its Taylor series to avoid cancellation.
constexpr double SMALL_THETA_THRESHOLD = 1e-6;
/// Relative tolerance used to deduplicate scheduled reflection breakpoints.
constexpr double CORNER_DEDUP_RELTOL = 1e-12;
/// Minimum absolute tolerance for breakpoint comparisons near zero time.
constexpr double CORNER_DEDUP_ABSTOL = 1e-21;
/// Minimum slope jump (in Volts per second times delay, i.e. Volts) that schedules a downstream reflection corner.
constexpr double WAVE_KINK_THRESHOLD_VOLTS = 1e-5;
/// Floor fraction of `stop_time` for `MaxTimeStep` so picosecond stubs inside second-long runs switch to semi-implicit.
constexpr double MIN_STOP_TIME_STEP_SHARE = 1e-4;

constexpr std::string_view LOSSY_TRANSIENT_REFUSAL =
    "Nodalys transient analysis supports lossless transmission lines only; lines with conductor or dielectric loss "
    "(Rdc, Rskin, Gdc, or loss tangent) are simulated in frequency-domain and Harmonic Balance analyses";

/**
 * @struct  FrequencyCoefficients
 * @brief   Complex coefficients of the two voltage-scaled constitutive branch equations at angular frequency
 *          \f$\omega\f$.
 */
struct FrequencyCoefficients {
    std::complex<double> VoltageOdd;
    std::complex<double> CurrentOdd;
    std::complex<double> VoltageEven;
    std::complex<double> CurrentEven;
};

/**
 * @brief   Evaluates \f$(1 - e^{-\theta}) / \theta\f$ accurately across \f$\theta \to 0\f$ and large
 *          \f$\operatorname{Re}(\theta)\f$.
 */
std::complex<double> Expm1Div(const std::complex<double> theta) {
    if (std::abs(theta) < SMALL_THETA_THRESHOLD) {
        const std::complex<double> t2 = theta * theta;
        return 1.0 - 0.5 * theta + t2 / 6.0 - (t2 * theta) / 24.0;
    }
    return (1.0 - std::exp(-theta)) / theta;
}

double Expm1DivReal(const double theta) {
    if (std::fabs(theta) < SMALL_THETA_THRESHOLD) {
        const double t2 = theta * theta;
        return 1.0 - 0.5 * theta + t2 / 6.0 - (t2 * theta) / 24.0;
    }
    return -std::expm1(-theta) / theta;
}

bool IsAfterCorner(const double candidate, const double reference) {
    return candidate
         > reference + CORNER_DEDUP_RELTOL * std::max(std::fabs(candidate), std::fabs(reference)) + CORNER_DEDUP_ABSTOL;
}

FrequencyCoefficients EvaluateFrequencyCoefficients(const TransmissionLineParameters &params,
                                                    const double total_inductance,
                                                    const double total_capacitance,
                                                    const double omega) {
    const double frequency_hz = omega / (2.0 * std::numbers::pi);
    double r_total = params.DcResistanceOhms;
    if (params.SkinResistanceOhms > 0.0 && params.ReferenceFrequencyHz > 0.0 && frequency_hz > 0.0) {
        r_total += params.SkinResistanceOhms * std::sqrt(frequency_hz / params.ReferenceFrequencyHz);
    }
    const double g_total = params.DcConductanceSiemens + omega * total_capacitance * params.LossTangent;

    const std::complex<double> z_total(r_total, omega * total_inductance);
    const std::complex<double> y_total(g_total, omega * total_capacitance);
    std::complex<double> theta = std::sqrt(z_total * y_total);
    if (theta.real() < 0.0) {
        theta = -theta;
    }

    const std::complex<double> exp_neg = std::exp(-theta);
    const std::complex<double> half_c = 0.5 * (1.0 + exp_neg);
    const std::complex<double> s = Expm1Div(theta);
    const double z0 = params.ImpedanceOhms;

    return {
        .VoltageOdd = half_c,
        .CurrentOdd = -0.5 * z_total * s,
        .VoltageEven = 0.5 * z0 * y_total * s,
        .CurrentEven = -z0 * half_c,
    };
}

} // namespace

/**
 * @brief   Constructs a uniform two-port TEM transmission line device.
 * @param[in] p1_pos   Port 1 positive MNA node unknown index (`-1` for ground).
 * @param[in] p1_neg   Port 1 negative MNA node unknown index (`-1` for ground).
 * @param[in] p2_pos   Port 2 positive MNA node unknown index (`-1` for ground).
 * @param[in] p2_neg   Port 2 negative MNA node unknown index (`-1` for ground).
 * @param[in] branch1  Allocated branch-current unknown index for port 1.
 * @param[in] branch2  Allocated branch-current unknown index for port 2.
 * @param[in] params   Electrical impedance, delay, and optional conductor/dielectric loss parameters.
 */
TransmissionLineDevice::TransmissionLineDevice(const int p1_pos,
                                               const int p1_neg,
                                               const int p2_pos,
                                               const int p2_neg,
                                               const int branch1,
                                               const int branch2,
                                               const TransmissionLineParameters &params)
    : m_P1Pos(p1_pos), m_P1Neg(p1_neg), m_P2Pos(p2_pos), m_P2Neg(p2_neg), m_Branch1(branch1), m_Branch2(branch2),
      m_Params(params), m_TotalInductance(params.ImpedanceOhms * params.DelaySeconds),
      m_TotalCapacitance(params.DelaySeconds / params.ImpedanceOhms) {
    const double theta0 = std::sqrt(m_Params.DcResistanceOhms * m_Params.DcConductanceSiemens);
    const double half_c0 = 0.5 * (1.0 + std::exp(-theta0));
    const double s0 = Expm1DivReal(theta0);
    m_Dc = {
        .VoltageOdd = half_c0,
        .CurrentOdd = -0.5 * m_Params.DcResistanceOhms * s0,
        .VoltageEven = 0.5 * m_Params.ImpedanceOhms * m_Params.DcConductanceSiemens * s0,
        .CurrentEven = -m_Params.ImpedanceOhms * half_c0,
    };
}

void TransmissionLineDevice::Stamp(MnaSystem &sys, const std::vector<double> & /*x*/, const TimeContext &time) {
    StampKcl(sys);
    if (!time.IsTransient) {
        if (time.IsTransientStart) {
            m_History.clear();
            m_PendingCorners.clear();
            m_LastStepTime = 0.0;
            m_StartedFromUic = false;
            m_HasPrevSlope = false;
        }
        StampDcBranches(sys);
        return;
    }
    StampTransientBergeron(sys, time);
}

double TransmissionLineDevice::TerminalCurrent(const int terminal, const std::vector<double> &x) const {
    const double i1 = x[static_cast<std::size_t>(m_Branch1)];
    const double i2 = x[static_cast<std::size_t>(m_Branch2)];
    switch (terminal) {
    case 0:
        return i1;
    case 1:
        return -i1;
    case 2:
        return i2;
    case 3:
    default:
        return -i2;
    }
}

std::complex<double> TransmissionLineDevice::AcTerminalCurrent(const int terminal,
                                                               const std::vector<double> & /*x_dc*/,
                                                               const std::vector<std::complex<double>> &x_ac,
                                                               const double /*omega*/) const {
    const std::complex<double> i1 = x_ac[static_cast<std::size_t>(m_Branch1)];
    const std::complex<double> i2 = x_ac[static_cast<std::size_t>(m_Branch2)];
    switch (terminal) {
    case 0:
        return i1;
    case 1:
        return -i1;
    case 2:
        return i2;
    case 3:
    default:
        return -i2;
    }
}

double TransmissionLineDevice::OutputNoiseVariance(const std::vector<double> & /*x_dc*/,
                                                   const std::vector<std::complex<double>> &adjoint,
                                                   const double frequency_hz) const {
    if (!m_Params.IsLossy() || frequency_hz <= 0.0) {
        return 0.0;
    }
    const double omega = 2.0 * std::numbers::pi * frequency_hz;
    const FrequencyCoefficients coeff =
        EvaluateFrequencyCoefficients(m_Params, m_TotalInductance, m_TotalCapacitance, omega);

    // Odd and even port admittances: i_odd = -(VoltageOdd / CurrentOdd) * v_odd,
    // i_even = -(VoltageEven / CurrentEven) * v_even.
    double g_odd = 0.0;
    if (std::abs(coeff.CurrentOdd) > 0.0) {
        g_odd = std::max(0.0, (-coeff.VoltageOdd / coeff.CurrentOdd).real());
    }
    double g_even = 0.0;
    if (std::abs(coeff.CurrentEven) > 0.0) {
        g_even = std::max(0.0, (-coeff.VoltageEven / coeff.CurrentEven).real());
    }

    const std::complex<double> lambda1 = AcBranchVoltage(adjoint, m_P1Pos, m_P1Neg);
    const std::complex<double> lambda2 = AcBranchVoltage(adjoint, m_P2Pos, m_P2Neg);
    const double even_norm = std::norm(lambda1 + lambda2);
    const double odd_norm = std::norm(lambda1 - lambda2);
    return 0.5 * m_FourKt * (g_even * even_norm + g_odd * odd_norm);
}

void TransmissionLineDevice::SetTemperature(const ThermalConditions &thermal) {
    m_FourKt = GetFourKt(thermal);
}

std::string_view TransmissionLineDevice::TransientUnsupportedReason() const {
    return m_Params.IsLossy() ? LOSSY_TRANSIENT_REFUSAL : std::string_view{};
}

void TransmissionLineDevice::StampFrequencyDelta(const ComplexStamper &stamper, const double omega) const {
    const FrequencyCoefficients ac =
        EvaluateFrequencyCoefficients(m_Params, m_TotalInductance, m_TotalCapacitance, omega);

    const std::complex<double> d_v_odd = ac.VoltageOdd - m_Dc.VoltageOdd;
    const std::complex<double> d_i_odd = ac.CurrentOdd - m_Dc.CurrentOdd;
    const std::complex<double> d_v_even = ac.VoltageEven - m_Dc.VoltageEven;
    const std::complex<double> d_i_even = ac.CurrentEven - m_Dc.CurrentEven;

    // Odd-mode branch row (m_Branch1): VoltageOdd * (V1 - V2) + CurrentOdd * (i1 - i2) = 0
    stamper.Add(m_Branch1, m_P1Pos, d_v_odd);
    stamper.Add(m_Branch1, m_P1Neg, -d_v_odd);
    stamper.Add(m_Branch1, m_P2Pos, -d_v_odd);
    stamper.Add(m_Branch1, m_P2Neg, d_v_odd);
    stamper.Add(m_Branch1, m_Branch1, d_i_odd);
    stamper.Add(m_Branch1, m_Branch2, -d_i_odd);

    // Even-mode branch row (m_Branch2): VoltageEven * (V1 + V2) + CurrentEven * (i1 + i2) = 0
    stamper.Add(m_Branch2, m_P1Pos, d_v_even);
    stamper.Add(m_Branch2, m_P1Neg, -d_v_even);
    stamper.Add(m_Branch2, m_P2Pos, d_v_even);
    stamper.Add(m_Branch2, m_P2Neg, -d_v_even);
    stamper.Add(m_Branch2, m_Branch1, d_i_even);
    stamper.Add(m_Branch2, m_Branch2, d_i_even);
}

void TransmissionLineDevice::AcceptStep(const std::vector<double> &x) {
    const double z0 = m_Params.ImpedanceOhms;
    const double v1 = BranchVoltage(x, m_P1Pos, m_P1Neg);
    const double v2 = BranchVoltage(x, m_P2Pos, m_P2Neg);
    const double i1 = x[static_cast<std::size_t>(m_Branch1)];
    const double i2 = x[static_cast<std::size_t>(m_Branch2)];
    const double w1 = v1 + z0 * i1;
    const double w2 = v2 + z0 * i2;

    double last_dt = 0.0;
    m_WaveChangedOnAccept = DetectWaveKink(w1, w2, last_dt);
    m_History.push_back({.Time = m_LastStepTime, .WaveFromPort1 = w1, .WaveFromPort2 = w2});

    // Prune samples strictly older than (m_LastStepTime - DelaySeconds), keeping one sample before the cutoff for
    // linear interpolation.
    const double cutoff = m_LastStepTime - m_Params.DelaySeconds;
    while (m_History.size() > 2 && m_History[1].Time <= cutoff) {
        m_History.pop_front();
    }
    while (!m_PendingCorners.empty() && !IsAfterCorner(m_PendingCorners.front(), m_LastStepTime)) {
        m_PendingCorners.pop_front();
    }
    if (m_WaveChangedOnAccept && last_dt <= 2.0 * m_Params.DelaySeconds) {
        NoteCorner(m_LastStepTime, 0.0);
    }
}

void TransmissionLineDevice::RejectStep() {
    if (!m_History.empty()) {
        m_LastStepTime = m_History.back().Time;
    }
}

double TransmissionLineDevice::NextBreakpoint(const double time) const {
    for (const double corner : m_PendingCorners) {
        if (IsAfterCorner(corner, time)) {
            return corner;
        }
    }
    return std::numeric_limits<double>::infinity();
}

void TransmissionLineDevice::NoteCorner(const double time, const double max_step) {
    if (max_step > 2.0 * m_Params.DelaySeconds) {
        return;
    }
    const double arrival = time + m_Params.DelaySeconds;
    const auto pos = std::ranges::lower_bound(m_PendingCorners, arrival);
    if (pos != m_PendingCorners.end() && !IsAfterCorner(*pos, arrival) && !IsAfterCorner(arrival, *pos)) {
        return;
    }
    if (pos != m_PendingCorners.begin()) {
        const auto prev = std::prev(pos);
        if (!IsAfterCorner(arrival, *prev)) {
            return;
        }
    }
    m_PendingCorners.insert(pos, arrival);
}

double TransmissionLineDevice::MaxTimeStep(const double stop_time) const {
    const double half_delay = 0.5 * m_Params.DelaySeconds;
    if (stop_time <= 0.0 || !std::isfinite(stop_time)) {
        return half_delay;
    }
    return std::max(half_delay, stop_time * MIN_STOP_TIME_STEP_SHARE);
}

std::unique_ptr<Device> TransmissionLineDevice::Clone() const {
    auto copy =
        std::make_unique<TransmissionLineDevice>(m_P1Pos, m_P1Neg, m_P2Pos, m_P2Neg, m_Branch1, m_Branch2, m_Params);
    copy->m_FourKt = m_FourKt;
    return copy;
}

bool TransmissionLineDevice::DetectWaveKink(const double w1, const double w2, double &step_dt) {
    step_dt = 0.0;
    if (m_History.empty()) {
        return m_StartedFromUic && std::max(std::fabs(w1), std::fabs(w2)) > WAVE_KINK_THRESHOLD_VOLTS;
    }
    const HistorySample &prev = m_History.back();
    const double dt = m_LastStepTime - prev.Time;
    step_dt = dt;
    if (dt <= 0.0) {
        return false;
    }
    const double slope1 = (w1 - prev.WaveFromPort1) / dt;
    const double slope2 = (w2 - prev.WaveFromPort2) / dt;
    bool changed = false;
    if (m_HasPrevSlope) {
        const double kink1 = std::fabs(slope1 - m_PrevSlope1) * m_Params.DelaySeconds;
        const double kink2 = std::fabs(slope2 - m_PrevSlope2) * m_Params.DelaySeconds;
        changed = std::max(kink1, kink2) > WAVE_KINK_THRESHOLD_VOLTS;
    } else {
        changed = std::max(std::fabs(w1 - prev.WaveFromPort1), std::fabs(w2 - prev.WaveFromPort2))
                > WAVE_KINK_THRESHOLD_VOLTS;
    }
    m_PrevSlope1 = slope1;
    m_PrevSlope2 = slope2;
    m_HasPrevSlope = true;
    return changed;
}

std::pair<double, double> TransmissionLineDevice::InterpolatePastWaves(const double query_time) const {
    if (m_History.empty()) {
        return {0.0, 0.0};
    }
    if (query_time <= m_History.front().Time) {
        if (m_StartedFromUic && query_time < 0.0) {
            return {0.0, 0.0};
        }
        return {m_History.front().WaveFromPort1, m_History.front().WaveFromPort2};
    }
    if (query_time >= m_History.back().Time) {
        return {m_History.back().WaveFromPort1, m_History.back().WaveFromPort2};
    }
    const auto upper = std::ranges::lower_bound(m_History, query_time, {}, &HistorySample::Time);
    if (upper == m_History.begin()) {
        return {upper->WaveFromPort1, upper->WaveFromPort2};
    }
    const auto lower = std::prev(upper);
    const double span = upper->Time - lower->Time;
    if (span <= 0.0) {
        return {upper->WaveFromPort1, upper->WaveFromPort2};
    }
    const double alpha = (query_time - lower->Time) / span;
    return {
        lower->WaveFromPort1 + alpha * (upper->WaveFromPort1 - lower->WaveFromPort1),
        lower->WaveFromPort2 + alpha * (upper->WaveFromPort2 - lower->WaveFromPort2),
    };
}

void TransmissionLineDevice::StampDcBranches(MnaSystem &sys) const {
    sys.AddA(m_Branch1, m_P1Pos, m_Dc.VoltageOdd);
    sys.AddA(m_Branch1, m_P1Neg, -m_Dc.VoltageOdd);
    sys.AddA(m_Branch1, m_P2Pos, -m_Dc.VoltageOdd);
    sys.AddA(m_Branch1, m_P2Neg, m_Dc.VoltageOdd);
    sys.AddA(m_Branch1, m_Branch1, m_Dc.CurrentOdd);
    sys.AddA(m_Branch1, m_Branch2, -m_Dc.CurrentOdd);

    sys.AddA(m_Branch2, m_P1Pos, m_Dc.VoltageEven);
    sys.AddA(m_Branch2, m_P1Neg, -m_Dc.VoltageEven);
    sys.AddA(m_Branch2, m_P2Pos, m_Dc.VoltageEven);
    sys.AddA(m_Branch2, m_P2Neg, -m_Dc.VoltageEven);
    sys.AddA(m_Branch2, m_Branch1, m_Dc.CurrentEven);
    sys.AddA(m_Branch2, m_Branch2, m_Dc.CurrentEven);
}

void TransmissionLineDevice::StampKcl(MnaSystem &sys) const {
    sys.AddA(m_P1Pos, m_Branch1, 1.0);
    sys.AddA(m_P1Neg, m_Branch1, -1.0);
    sys.AddA(m_P2Pos, m_Branch2, 1.0);
    sys.AddA(m_P2Neg, m_Branch2, -1.0);
}

void TransmissionLineDevice::StampShortDelayStep(MnaSystem &sys, const double step) const {
    const double z0 = m_Params.ImpedanceOhms;
    const double past_weight = m_Params.DelaySeconds / step;
    const double curr_weight = 1.0 - past_weight;
    const HistorySample &last = m_History.back();

    sys.AddA(m_Branch1, m_P1Pos, 1.0);
    sys.AddA(m_Branch1, m_P1Neg, -1.0);
    sys.AddA(m_Branch1, m_Branch1, -z0);
    sys.AddA(m_Branch1, m_P2Pos, -curr_weight);
    sys.AddA(m_Branch1, m_P2Neg, curr_weight);
    sys.AddA(m_Branch1, m_Branch2, -curr_weight * z0);
    sys.AddRhs(m_Branch1, past_weight * last.WaveFromPort2);

    sys.AddA(m_Branch2, m_P2Pos, 1.0);
    sys.AddA(m_Branch2, m_P2Neg, -1.0);
    sys.AddA(m_Branch2, m_Branch2, -z0);
    sys.AddA(m_Branch2, m_P1Pos, -curr_weight);
    sys.AddA(m_Branch2, m_P1Neg, curr_weight);
    sys.AddA(m_Branch2, m_Branch1, -curr_weight * z0);
    sys.AddRhs(m_Branch2, past_weight * last.WaveFromPort1);
}

void TransmissionLineDevice::StampTransientBergeron(MnaSystem &sys, const TimeContext &time) {
    const double z0 = m_Params.ImpedanceOhms;
    m_LastStepTime = time.Time;

    if (time.IsTransientStart && time.UseInitialConditions) {
        m_History.clear();
        m_PendingCorners.clear();
        m_StartedFromUic = true;
        m_HasPrevSlope = false;
        sys.AddA(m_Branch1, m_P1Pos, 1.0);
        sys.AddA(m_Branch1, m_P1Neg, -1.0);
        sys.AddA(m_Branch1, m_Branch1, -z0);
        sys.AddA(m_Branch2, m_P2Pos, 1.0);
        sys.AddA(m_Branch2, m_P2Neg, -1.0);
        sys.AddA(m_Branch2, m_Branch2, -z0);
        return;
    }

    const double td = m_Params.DelaySeconds;
    const double query_time = time.Time - td;
    const double prev_time = m_History.empty() ? 0.0 : m_History.back().Time;
    const double step = time.Time - prev_time;

    if (!m_History.empty() && step > td && query_time > prev_time) {
        StampShortDelayStep(sys, step);
        return;
    }

    const auto [wave_from_1, wave_from_2] = InterpolatePastWaves(query_time);
    sys.AddA(m_Branch1, m_P1Pos, 1.0);
    sys.AddA(m_Branch1, m_P1Neg, -1.0);
    sys.AddA(m_Branch1, m_Branch1, -z0);
    sys.AddRhs(m_Branch1, wave_from_2);

    sys.AddA(m_Branch2, m_P2Pos, 1.0);
    sys.AddA(m_Branch2, m_P2Neg, -1.0);
    sys.AddA(m_Branch2, m_Branch2, -z0);
    sys.AddRhs(m_Branch2, wave_from_1);
}

} // namespace Nodalys

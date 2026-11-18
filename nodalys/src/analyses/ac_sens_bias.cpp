/**
 * @file    ac_sens_bias.cpp
 * @brief   How the small-signal matrix of an AC sensitivity analysis moves with the DC operating point.
 */

#include "analyses/ac_sens_bias.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Nodalys {

namespace {

/// Relative size of the step that probes a stamp, which keeps the truncation error of a central difference on the
/// exponential of a junction (the step over the thermal voltage, squared) far below the tolerance of the solver.
constexpr double DERIVATIVE_STEP = 1e-6;

/// Smallest size a DC unknown is taken to have when it is probed, so an unknown at zero volts still moves.
constexpr double MIN_STEP_SCALE = 1.0;

/// The stamps of one device at one iterate: the matrix of the operating point and the reactive coefficients.
struct DeviceStamp {
    DenseMatrix Conductance;
    DenseMatrix Reactive;
};

/// Scratch systems that every stamp goes through, so the work does not allocate one per probe.
class StampProbe {
public:
    explicit StampProbe(const CompiledCircuit &compiled)
        : m_Conductance(MakeSystem(compiled)), m_Reactive(MakeSystem(compiled)) {}

    /**
     * @brief   Stamps one device at an iterate into the scratch systems.
     * @details A switch moves its state when it is stamped, so the caller stamps the device at the operating point
     *          last, which leaves it as the solve left it.
     */
    void Stamp(Device &device, const std::vector<double> &x) {
        m_Conductance.Clear();
        m_Reactive.Clear();
        device.Stamp(m_Conductance, x, TimeContext{});
        device.StampAc(m_Reactive, x);
    }

    const MnaSystem &GetConductance() const { return m_Conductance; }
    const MnaSystem &GetReactive() const { return m_Reactive; }

private:
    static MnaSystem MakeSystem(const CompiledCircuit &compiled) {
        std::vector<bool> node_flags(static_cast<std::size_t>(compiled.System->GetSize()), false);
        for (int index = 0; index < compiled.System->GetSize(); ++index) {
            node_flags[static_cast<std::size_t>(index)] = compiled.System->IsNodeUnknown(index);
        }
        return MnaSystem(std::move(node_flags));
    }

    MnaSystem m_Conductance;
    MnaSystem m_Reactive;
};

/// The unknowns a stamp reads: the columns where it has an entry.
std::vector<int> FindReadUnknowns(const StampProbe &probe, const int size) {
    std::vector<int> unknowns;
    for (int column = 0; column < size; ++column) {
        for (int row = 0; row < size; ++row) {
            const auto r = static_cast<std::size_t>(row);
            const auto c = static_cast<std::size_t>(column);
            if (probe.GetConductance().Matrix().At(r, c) != 0.0 || probe.GetReactive().Matrix().At(r, c) != 0.0) {
                unknowns.push_back(column);
                break;
            }
        }
    }
    return unknowns;
}

/**
 * @brief   Copies the matrices a probe holds, to subtract them from the ones of another iterate.
 */
DeviceStamp CopyStamp(const StampProbe &probe) {
    return {probe.GetConductance().Matrix(), probe.GetReactive().Matrix()};
}

/**
 * @brief   Central difference of the stamps of a device with respect to one DC unknown.
 */
void ProbeUnknown(StampProbe &probe,
                  Device &device,
                  const std::vector<double> &x_dc,
                  const int unknown,
                  const int size,
                  std::vector<JacobianDerivative> &derivatives) {
    const auto index = static_cast<std::size_t>(unknown);
    const double step = DERIVATIVE_STEP * std::max(MIN_STEP_SCALE, std::fabs(x_dc[index]));
    std::vector<double> x = x_dc;
    x[index] = x_dc[index] + step;
    probe.Stamp(device, x);
    const DeviceStamp above = CopyStamp(probe);
    x[index] = x_dc[index] - step;
    probe.Stamp(device, x);

    JacobianDerivative derivative{.Unknown = unknown, .Entries = {}};
    for (int row = 0; row < size; ++row) {
        for (int column = 0; column < size; ++column) {
            const auto r = static_cast<std::size_t>(row);
            const auto c = static_cast<std::size_t>(column);
            const double conductance =
                (above.Conductance.At(r, c) - probe.GetConductance().Matrix().At(r, c)) / (2.0 * step);
            const double reactive = (above.Reactive.At(r, c) - probe.GetReactive().Matrix().At(r, c)) / (2.0 * step);
            if (conductance != 0.0 || reactive != 0.0) {
                derivative.Entries.push_back(
                    {.Row = row, .Column = column, .Conductance = conductance, .Reactive = reactive});
            }
        }
    }
    if (!derivative.Entries.empty()) {
        derivatives.push_back(std::move(derivative));
    }
}

/// Adds the derivatives of one unknown to the total, summing entries of the same position.
void Merge(std::map<int, std::map<std::pair<int, int>, JacobianEntry>> &total,
           const std::vector<JacobianDerivative> &found) {
    for (const JacobianDerivative &derivative : found) {
        auto &entries = total[derivative.Unknown];
        for (const JacobianEntry &entry : derivative.Entries) {
            JacobianEntry &sum = entries[{entry.Row, entry.Column}];
            sum.Row = entry.Row;
            sum.Column = entry.Column;
            sum.Conductance += entry.Conductance;
            sum.Reactive += entry.Reactive;
        }
    }
}

} // namespace

/**
 * @brief   Differentiates the stamps of every nonlinear device with respect to the DC unknowns it reads.
 * @details Each device is stamped a little above and below the operating point, one unknown at a time, and the
 *          difference of the matrices, over the step, is the derivative: second derivatives of the device equations,
 *          which the devices do not expose and which every model, with its capacitances, gives the same way.
 * @param[in] compiled  Circuit whose devices are stamped; their state is left as the last stamp at the operating point
 *                      makes it.
 * @param[in] x_dc      Converged DC operating point.
 * @return  One derivative per unknown that some nonlinear device reads; empty for a linear circuit.
 */
std::vector<JacobianDerivative> FindJacobianDerivatives(const CompiledCircuit &compiled,
                                                        const std::vector<double> &x_dc) {
    const int size = compiled.System->GetSize();
    StampProbe probe(compiled);
    std::map<int, std::map<std::pair<int, int>, JacobianEntry>> total;
    for (const std::unique_ptr<Device> &device : compiled.Devices) {
        if (device->IsLinear()) {
            continue;
        }
        probe.Stamp(*device, x_dc);
        std::vector<JacobianDerivative> found;
        for (const int unknown : FindReadUnknowns(probe, size)) {
            ProbeUnknown(probe, *device, x_dc, unknown, size, found);
        }
        probe.Stamp(*device, x_dc);
        Merge(total, found);
    }
    std::vector<JacobianDerivative> derivatives;
    for (const auto &[unknown, entries] : total) {
        JacobianDerivative derivative{.Unknown = unknown, .Entries = {}};
        for (const auto &[position, entry] : entries) {
            derivative.Entries.push_back(entry);
        }
        derivatives.push_back(std::move(derivative));
    }
    return derivatives;
}

/**
 * @brief   Weighs how the output of the AC solution moves with each DC unknown, at one frequency.
 * @details The weight of unknown k is $-\lambda^T (\partial Y / \partial x_k) x_{ac}$, which the DC adjoint then turns
 *          into the change of the output for the change of the operating point that a value causes.
 * @param[in] derivatives  Derivatives of the nonlinear stamps.
 * @param[in] size         Number of unknowns of the system.
 * @param[in] adjoint      Solution of the transposed small-signal system for the output.
 * @param[in] x_ac         Small-signal solution at the frequency.
 * @param[in] omega        Angular frequency in radians per second.
 * @return  One weight per unknown, zero where no nonlinear device reads it.
 */
std::vector<std::complex<double>> FindBiasWeights(const std::vector<JacobianDerivative> &derivatives,
                                                  const std::size_t size,
                                                  const std::vector<std::complex<double>> &adjoint,
                                                  const std::vector<std::complex<double>> &x_ac,
                                                  const double omega) {
    std::vector<std::complex<double>> weights(size, std::complex<double>{0.0, 0.0});
    for (const JacobianDerivative &derivative : derivatives) {
        std::complex<double> sum{0.0, 0.0};
        for (const JacobianEntry &entry : derivative.Entries) {
            const std::complex<double> change(entry.Conductance, omega * entry.Reactive);
            sum += adjoint[static_cast<std::size_t>(entry.Row)] * change * x_ac[static_cast<std::size_t>(entry.Column)];
        }
        weights[static_cast<std::size_t>(derivative.Unknown)] = -sum;
    }
    return weights;
}

} // namespace Nodalys

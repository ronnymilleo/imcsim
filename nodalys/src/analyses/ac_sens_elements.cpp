/**
 * @file    ac_sens_elements.cpp
 * @brief   The elements an AC sensitivity analysis varies, and the derivative of the output with respect to each.
 */

#include "analyses/ac_sens_elements.h"

#include "devices/controlled_sources.h"
#include "devices/independent_sources.h"
#include "devices/passive_devices.h"
#include "text/text_case.h"
#include <optional>

namespace Nodalys {

namespace {

using Complex = std::complex<double>;

/// A value of a vector at an unknown, with ground (-1) reading zero.
template <typename Value>
Value At(const std::vector<Value> &values, const int unknown) {
    return unknown < 0 ? Value{} : values[static_cast<std::size_t>(unknown)];
}

/// The difference of a vector between two unknowns.
template <typename Value>
Value Across(const std::vector<Value> &values, const int first, const int second) {
    return At(values, first) - At(values, second);
}

/**
 * @brief   The first element of a pair with its value, or nothing when the device is none of the kinds that are varied.
 */
std::optional<AcSensitivityElement> DescribeElement(const std::string &name, const Device &device) {
    AcSensitivityElement element{.Name = ToLower(name), .Parameter = {}, .Instance = &device, .Value = 0.0};
    if (const auto *resistor = dynamic_cast<const ResistorDevice *>(&device)) {
        element.Parameter = SensitivityParameter::RESISTANCE;
        element.Value = resistor->GetResistance();
        return element.Value > 0.0 ? std::optional(element) : std::nullopt;
    }
    if (const auto *capacitor = dynamic_cast<const CapacitorDevice *>(&device)) {
        element.Parameter = SensitivityParameter::CAPACITANCE;
        element.Value = capacitor->GetCapacitance();
    } else if (const auto *inductor = dynamic_cast<const InductorDevice *>(&device)) {
        element.Parameter = SensitivityParameter::INDUCTANCE;
        element.Value = inductor->GetInductance();
    } else if (const auto *voltage_source = dynamic_cast<const VoltageSourceDevice *>(&device)) {
        element.Parameter = SensitivityParameter::VOLTAGE;
        element.Value = voltage_source->GetDcValue();
    } else if (const auto *current_source = dynamic_cast<const CurrentSourceDevice *>(&device)) {
        element.Parameter = SensitivityParameter::CURRENT;
        element.Value = current_source->GetDcValue();
    } else if (const auto *vcvs = dynamic_cast<const VcvsDevice *>(&device)) {
        element.Parameter = SensitivityParameter::GAIN;
        element.Value = vcvs->GetGain();
    } else if (const auto *vccs = dynamic_cast<const VccsDevice *>(&device)) {
        element.Parameter = SensitivityParameter::GAIN;
        element.Value = vccs->GetGain();
    } else if (const auto *cccs = dynamic_cast<const CccsDevice *>(&device)) {
        element.Parameter = SensitivityParameter::GAIN;
        element.Value = cccs->GetGain();
    } else if (const auto *ccvs = dynamic_cast<const CcvsDevice *>(&device)) {
        element.Parameter = SensitivityParameter::GAIN;
        element.Value = ccvs->GetGain();
    } else {
        return std::nullopt;
    }
    return element;
}

/// Passive elements: the value is in the small-signal matrix and, for a resistor, in the DC equations.
Complex FindPassiveDerivative(const AcSensitivityElement &element, const AcSensitivityPoint &point) {
    const Complex j_omega(0.0, point.Omega);
    switch (element.Parameter) {
    case SensitivityParameter::RESISTANCE: {
        // Y holds 1/R between the nodes, whose derivative is -1/R^2 across them
        const auto &resistor = static_cast<const ResistorDevice &>(*element.Instance);
        const double resistance = resistor.GetResistance();
        const int a = resistor.GetNodeA();
        const int b = resistor.GetNodeB();
        const Complex direct = Across(point.Adjoint, a, b) * Across(point.AcSolution, a, b);
        const Complex bias = Across(point.BiasAdjoint, a, b) * Across(point.DcSolution, a, b);
        return (direct + bias) / (resistance * resistance);
    }
    case SensitivityParameter::CAPACITANCE: {
        const auto &capacitor = static_cast<const CapacitorDevice &>(*element.Instance);
        const int a = capacitor.GetNodeA();
        const int b = capacitor.GetNodeB();
        return -j_omega * Across(point.Adjoint, a, b) * Across(point.AcSolution, a, b);
    }
    case SensitivityParameter::INDUCTANCE: {
        const auto &inductor = static_cast<const InductorDevice &>(*element.Instance);
        const int branch = inductor.GetBranch();
        return j_omega * At(point.Adjoint, branch) * At(point.AcSolution, branch);
    }
    default:
        return {};
    }
}

/// Independent sources: the DC value only moves the operating point, so a linear circuit does not feel it.
Complex FindSourceDerivative(const AcSensitivityElement &element, const AcSensitivityPoint &point) {
    if (element.Parameter == SensitivityParameter::VOLTAGE) {
        const auto &source = static_cast<const VoltageSourceDevice &>(*element.Instance);
        return At(point.BiasAdjoint, source.GetBranch());
    }
    const auto &source = static_cast<const CurrentSourceDevice &>(*element.Instance);
    return Across(point.BiasAdjoint, source.GetNodeB(), source.GetNodeA());
}

/// Controlled sources: the gain sits in the small-signal matrix and, in the DC equations, in the same entries.
Complex FindControlledDerivative(const AcSensitivityElement &element, const AcSensitivityPoint &point) {
    if (const auto *vcvs = dynamic_cast<const VcvsDevice *>(element.Instance)) {
        const int branch = vcvs->GetBranch();
        const Complex direct = At(point.Adjoint, branch)
                             * Across(point.AcSolution, vcvs->GetControlPositive(), vcvs->GetControlNegative());
        const Complex bias = At(point.BiasAdjoint, branch)
                           * Across(point.DcSolution, vcvs->GetControlPositive(), vcvs->GetControlNegative());
        return direct + bias;
    }
    if (const auto *vccs = dynamic_cast<const VccsDevice *>(element.Instance)) {
        const int a = vccs->GetNodeA();
        const int b = vccs->GetNodeB();
        const int positive = vccs->GetControlPositive();
        const int negative = vccs->GetControlNegative();
        return -Across(point.Adjoint, a, b) * Across(point.AcSolution, positive, negative)
             - Across(point.BiasAdjoint, a, b) * Across(point.DcSolution, positive, negative);
    }
    if (const auto *cccs = dynamic_cast<const CccsDevice *>(element.Instance)) {
        const int a = cccs->GetNodeA();
        const int b = cccs->GetNodeB();
        const int control = cccs->ControlBranch();
        return -Across(point.Adjoint, a, b) * At(point.AcSolution, control)
             - Across(point.BiasAdjoint, a, b) * At(point.DcSolution, control);
    }
    const auto &ccvs = static_cast<const CcvsDevice &>(*element.Instance);
    const int branch = ccvs.GetBranch();
    const int control = ccvs.ControlBranch();
    return At(point.Adjoint, branch) * At(point.AcSolution, control)
         + At(point.BiasAdjoint, branch) * At(point.DcSolution, control);
}

} // namespace

/**
 * @brief   Lists the elements whose value an AC sensitivity analysis varies.
 * @param[in] compiled  Compiled circuit with its elements by name.
 * @return  Resistors, capacitors, inductors, independent sources and controlled sources, by lower case name; the same
 *          set the DC sensitivity covers, with the reactive elements added.
 */
std::vector<AcSensitivityElement> FindSensitivityElements(const CompiledCircuit &compiled) {
    std::vector<AcSensitivityElement> elements;
    for (const auto &[name, device] : compiled.ElementsByName) {
        if (device == nullptr) {
            continue;
        }
        if (std::optional<AcSensitivityElement> element = DescribeElement(name, *device)) {
            elements.push_back(std::move(*element));
        }
    }
    return elements;
}

/**
 * @brief   Works out how the output changes with the value of one element at one frequency.
 * @param[in] element  Element to vary.
 * @param[in] point    Solutions and adjoints at the frequency.
 * @return  The derivative in volts per unit of the value.
 */
std::complex<double> FindOutputDerivative(const AcSensitivityElement &element, const AcSensitivityPoint &point) {
    switch (element.Parameter) {
    case SensitivityParameter::RESISTANCE:
    case SensitivityParameter::CAPACITANCE:
    case SensitivityParameter::INDUCTANCE:
        return FindPassiveDerivative(element, point);
    case SensitivityParameter::VOLTAGE:
    case SensitivityParameter::CURRENT:
        return FindSourceDerivative(element, point);
    case SensitivityParameter::GAIN:
        return FindControlledDerivative(element, point);
    default:
        return {};
    }
}

} // namespace Nodalys

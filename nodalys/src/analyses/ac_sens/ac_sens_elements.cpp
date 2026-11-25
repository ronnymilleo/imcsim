/**
 * @file    ac_sens_elements.cpp
 * @brief   The elements an AC sensitivity analysis varies, and the derivative of the output with respect to each.
 */

#include "analyses/ac_sens/ac_sens_elements.h"

#include "text/text_case.h"
#include <optional>

namespace Nodalys {

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
        const SensitiveDevice *sensitive = device->AsSensitive();
        if (sensitive == nullptr) {
            continue;
        }
        if (const std::optional<SensitivityDescriptor> descriptor = sensitive->DescribeSensitivity()) {
            elements.push_back(AcSensitivityElement{
                .Name = ToLower(name),
                .Parameter = descriptor->Parameter,
                .Instance = sensitive,
                .Value = descriptor->Value,
            });
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
    if (element.Instance == nullptr) {
        return {};
    }
    const AcAdjointView view{
        .Omega = point.Omega,
        .DcSolution = point.DcSolution,
        .AcSolution = point.AcSolution,
        .Adjoint = point.Adjoint,
        .BiasAdjoint = point.BiasAdjoint,
    };
    return element.Instance->AcSensitivityDerivative(view);
}

} // namespace Nodalys

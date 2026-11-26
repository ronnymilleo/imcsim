/**
 * @file    line_geometry_calculator.cpp
 * @brief   The popup of the transmission line properties that works out Z0 and the losses from a real line's geometry.
 */

#include "windows/line_geometry_calculator.h"

#include "base/theme.h"
#include "spice/spice_value.h"
#include <expected>
#include <imgui.h>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace GUI {

namespace {

constexpr const char *POPUP_ID = "Line geometry";
/// Lengths are in meters with a scale suffix, as every value of the editor is: "1.6m" is 1.6 mm.
constexpr const char *LENGTH_UNIT = "m";

/**
 * @brief   One dimension of a cross-section as the calculator shows it.
 */
struct DimensionField {
    const char *Label;
    double *Meters;
};

/**
 * @brief   The dimensions of the cross-section on show, pointing into it, in the order the fields list them.
 */
std::vector<DimensionField> ListDimensions(Core::LineCrossSection &section) {
    return std::visit(
        [](auto &shape) -> std::vector<DimensionField> {
            using Shape = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<Shape, Core::CoaxGeometry>) {
                return {{"Inner diameter", &shape.InnerDiameter}, {"Shield diameter", &shape.OuterDiameter}};
            } else if constexpr (std::is_same_v<Shape, Core::MicrostripGeometry>) {
                return {{"Width", &shape.Width}, {"Height", &shape.Height}, {"Thickness", &shape.Thickness}};
            } else {
                return {{"Width", &shape.Width},
                        {"Plane spacing", &shape.PlaneSpacing},
                        {"Thickness", &shape.Thickness}};
            }
        },
        section);
}

void LoadFields(LineGeometryCalculator &calculator) {
    const std::vector<DimensionField> dimensions = ListDimensions(calculator.Geometry.CrossSection);
    for (std::size_t index = 0; index < dimensions.size(); ++index) {
        calculator.Dimensions[index].Load(*dimensions[index].Meters);
    }
    const Core::LineMaterials &materials = calculator.Geometry.Materials;
    calculator.Permittivity.Load(materials.RelativePermittivity);
    calculator.LossTangent.Load(materials.LossTangent);
    calculator.Conductivity.Load(materials.Conductivity);
    calculator.Length.Load(calculator.Geometry.Length);
    calculator.ReferenceFrequency.Load(calculator.Geometry.ReferenceFrequency);
}

bool IsPositive(const double value) {
    return value > 0.0;
}

/**
 * @brief   One choice of cross-section; picking another starts it from its own defaults, and picking the one on show
 *          keeps what was typed.
 */
template <typename Shape>
bool DrawCrossSectionButton(const char *label, Core::LineCrossSection &section) {
    const bool is_shown = std::holds_alternative<Shape>(section);
    if (!ImGui::RadioButton(label, is_shown) || is_shown) {
        return false;
    }
    section = Shape{};
    return true;
}

void DrawCrossSectionChoice(LineGeometryCalculator &calculator) {
    Core::LineCrossSection &section = calculator.Geometry.CrossSection;
    bool changed = DrawCrossSectionButton<Core::MicrostripGeometry>("Microstrip", section);
    ImGui::SameLine();
    changed = DrawCrossSectionButton<Core::StriplineGeometry>("Stripline", section) || changed;
    ImGui::SameLine();
    changed = DrawCrossSectionButton<Core::CoaxGeometry>("Coax", section) || changed;
    if (changed) {
        LoadFields(calculator);
    }
}

void DrawGeometryFields(LineGeometryCalculator &calculator) {
    const std::vector<DimensionField> dimensions = ListDimensions(calculator.Geometry.CrossSection);
    for (std::size_t index = 0; index < dimensions.size(); ++index) {
        const DimensionField &dimension = dimensions[index];
        if (const std::optional<double> value =
                calculator.Dimensions[index].Draw(dimension.Label, LENGTH_UNIT, IsPositive)) {
            *dimension.Meters = *value;
        }
    }
    Core::LineMaterials &materials = calculator.Geometry.Materials;
    if (const std::optional<double> value =
            calculator.Permittivity.Draw("Permittivity", "", [](const double candidate) { return candidate >= 1.0; })) {
        materials.RelativePermittivity = *value;
    }
    if (const std::optional<double> value =
            calculator.LossTangent.Draw("Loss tangent", "", [](const double candidate) { return candidate >= 0.0; })) {
        materials.LossTangent = *value;
    }
    if (const std::optional<double> value = calculator.Conductivity.Draw("Conductivity", "S/m", IsPositive)) {
        materials.Conductivity = *value;
    }
    if (const std::optional<double> value = calculator.Length.Draw("Length", LENGTH_UNIT, IsPositive)) {
        calculator.Geometry.Length = *value;
    }
    if (const std::optional<double> value = calculator.ReferenceFrequency.Draw("Ref freq", "Hz", IsPositive)) {
        calculator.Geometry.ReferenceFrequency = *value;
    }
}

void DrawFigures(const Core::LineElectrical &line) {
    const Core::TransmissionLineParameters &parameters = line.Parameters;
    ImGui::Text("Z0 %s Ohm, effective permittivity %s",
                Core::FormatValue(line.CharacteristicImpedance).c_str(),
                Core::FormatValue(line.EffectivePermittivity).c_str());
    ImGui::Text("Delay %ss", Core::FormatValue(parameters.Delay).c_str());
    ImGui::Text("DC R %s Ohm, skin R %s Ohm at %sHz",
                Core::FormatValue(parameters.DcResistance).c_str(),
                Core::FormatValue(parameters.SkinResistance).c_str(),
                Core::FormatValue(parameters.ReferenceFrequency).c_str());
    ImGui::Text("Loss tangent the wave sees %s", Core::FormatValue(parameters.LossTangent).c_str());
}

} // namespace

/**
 * @brief   Opens the calculator, with the fields of the geometry it kept from its last use.
 * @param[in,out] calculator  Geometry and fields of the calculator.
 */
void OpenLineGeometryCalculator(LineGeometryCalculator &calculator) {
    LoadFields(calculator);
    ImGui::OpenPopup(POPUP_ID);
}

/**
 * @brief   Draws the calculator while it is open: the cross-section, its dimensions and materials, and the figures they
 *          give.
 * @param[in,out] calculator  Geometry and fields of the calculator, changed as the user types.
 * @return  The figures to give the line when the user applies them; none while the popup is closed or open.
 * @note    Apply is disabled, with the reason shown, while the geometry is not a line.
 */
std::optional<Core::LineElectrical> DrawLineGeometryCalculator(LineGeometryCalculator &calculator) {
    if (!ImGui::BeginPopup(POPUP_ID)) {
        return std::nullopt;
    }
    DrawCrossSectionChoice(calculator);
    DrawGeometryFields(calculator);
    ImGui::Separator();

    std::optional<Core::LineElectrical> applied;
    const std::expected<Core::LineElectrical, std::string> line = Core::ComputeLineElectrical(calculator.Geometry);
    if (line) {
        DrawFigures(*line);
    } else {
        ImGui::TextColored(GetErrorTextColor(), "%s", line.error().c_str());
    }
    ImGui::BeginDisabled(!line.has_value());
    if (ImGui::Button("Apply")) {
        applied = *line;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::TextDisabled("Closed-form figures, within a few percent; smooth copper thicker than the skin depth");
    ImGui::EndPopup();
    return applied;
}

} // namespace GUI

/**
 * @file    line_geometry.cpp
 * @brief   Works out the electrical figures of a transmission line from its cross-section, materials and length.
 */

#include "circuit/line_geometry.h"

#include <cmath>
#include <numbers>
#include <optional>
#include <type_traits>
#include <utility>

namespace Core {

namespace {

/// Speed of light in vacuum, in m/s.
constexpr double SPEED_OF_LIGHT = 299792458.0;
/// Permeability of vacuum, in H/m; the conductors are taken as non-magnetic.
constexpr double VACUUM_PERMEABILITY = 4e-7 * std::numbers::pi;
/// Impedance of free space, in Ohms.
constexpr double FREE_SPACE_IMPEDANCE = VACUUM_PERMEABILITY * SPEED_OF_LIGHT;
/// Below this distance from 1, a permittivity counts as air, whose filling factor is taken as 1.
constexpr double AIR_PERMITTIVITY_MARGIN = 1e-9;

/**
 * @brief   The figures of a cross-section per meter of line, before the length and the frequency are applied.
 */
struct CrossSectionFigures {
    double CharacteristicImpedance = 0.0;
    double EffectivePermittivity = 1.0;
    /// Share of the dielectric loss the wave sees: below 1 when part of the field runs in air, as on a microstrip.
    double FillingFactor = 1.0;
    double DcArea = 0.0;     ///< Conductor area the direct current flows through, in square meters.
    double SkinFactor = 0.0; ///< The skin resistance per meter is the surface resistance times this, in 1/m.
};

using FiguresOrError = std::expected<CrossSectionFigures, std::string>;

/**
 * @brief   Coax: \f$Z_0 = \eta_0 \ln(D/d) / (2\pi\sqrt{\varepsilon_r})\f$, with the field all in the dielectric.
 * @details The skin resistance adds the inner conductor and the inside of the shield, \f$R_s (1/a + 1/b) / 2\pi\f$; the
 *          DC resistance counts the inner conductor only, since the shield's thickness is not given and its larger
 *          section makes it the smaller share.
 */
FiguresOrError ComputeCoax(const CoaxGeometry &coax, const double relative_permittivity) {
    if (coax.InnerDiameter <= 0.0) {
        return std::unexpected("The inner diameter must be above zero");
    }
    if (coax.OuterDiameter <= coax.InnerDiameter) {
        return std::unexpected("The shield diameter must be larger than the inner diameter");
    }
    const double inner_radius = coax.InnerDiameter / 2.0;
    const double outer_radius = coax.OuterDiameter / 2.0;
    return CrossSectionFigures{
        .CharacteristicImpedance = FREE_SPACE_IMPEDANCE * std::log(coax.OuterDiameter / coax.InnerDiameter)
                                 / (2.0 * std::numbers::pi * std::sqrt(relative_permittivity)),
        .EffectivePermittivity = relative_permittivity,
        .FillingFactor = 1.0,
        .DcArea = std::numbers::pi * inner_radius * inner_radius,
        .SkinFactor = (1.0 / inner_radius + 1.0 / outer_radius) / (2.0 * std::numbers::pi),
    };
}

/**
 * @brief   Microstrip by Hammerstad and Jensen: the effective permittivity from the width over the height, and Z0 from
 *          the width widened by the copper thickness (Wheeler's correction).
 * @details The dielectric loss is scaled by the filling factor \f$\varepsilon_r(\varepsilon_{\text{eff}} - 1) /
 *          (\varepsilon_{\text{eff}}(\varepsilon_r - 1))\f$, since part of the field runs in air. The skin resistance
 *          is \f$2R_s/W\f$, the trace and the plane together (Pozar), which holds for traces wider than the substrate
 *          is thick and underestimates narrow ones.
 */
FiguresOrError ComputeMicrostrip(const MicrostripGeometry &strip, const double relative_permittivity) {
    if (strip.Width <= 0.0 || strip.Height <= 0.0 || strip.Thickness <= 0.0) {
        return std::unexpected("The width, height and thickness must be above zero");
    }
    const double width_ratio = strip.Width / strip.Height;
    const double narrow_term = width_ratio < 1.0 ? 0.04 * (1.0 - width_ratio) * (1.0 - width_ratio) : 0.0;
    const double effective =
        (relative_permittivity + 1.0) / 2.0
        + (relative_permittivity - 1.0) / 2.0 * (1.0 / std::sqrt(1.0 + 12.0 / width_ratio) + narrow_term);

    const double thickness_log = width_ratio >= 1.0 / (2.0 * std::numbers::pi)
                                   ? std::log(2.0 * strip.Height / strip.Thickness)
                                   : std::log(4.0 * std::numbers::pi * strip.Width / strip.Thickness);
    const double effective_width = strip.Width + strip.Thickness / std::numbers::pi * (1.0 + thickness_log);
    const double ratio = effective_width / strip.Height;
    const double impedance =
        ratio <= 1.0
            ? 60.0 / std::sqrt(effective) * std::log(8.0 / ratio + ratio / 4.0)
            : FREE_SPACE_IMPEDANCE / (std::sqrt(effective) * (ratio + 1.393 + 0.667 * std::log(ratio + 1.444)));

    const double filling = relative_permittivity - 1.0 < AIR_PERMITTIVITY_MARGIN
                             ? 1.0
                             : relative_permittivity * (effective - 1.0) / (effective * (relative_permittivity - 1.0));
    return CrossSectionFigures{
        .CharacteristicImpedance = impedance,
        .EffectivePermittivity = effective,
        .FillingFactor = filling,
        .DcArea = strip.Width * strip.Thickness,
        .SkinFactor = 2.0 / strip.Width,
    };
}

/**
 * @brief   Centered stripline by Cohn's formula as Pozar gives it, \f$Z_0 = 30\pi b / (\sqrt{\varepsilon_r}(W_e +
 *          0.441 b))\f$, with the narrow-strip correction of the width; the field is all in the dielectric.
 * @details The copper thickness counts only in the DC resistance: the formula is for a thin strip, and a thick one
 *          lowers Z0 by a few percent. The skin resistance is \f$R_s/W\f$, both faces of the trace and both planes.
 */
FiguresOrError ComputeStripline(const StriplineGeometry &strip, const double relative_permittivity) {
    if (strip.Width <= 0.0 || strip.PlaneSpacing <= 0.0 || strip.Thickness <= 0.0) {
        return std::unexpected("The width, plane spacing and thickness must be above zero");
    }
    if (strip.Thickness >= strip.PlaneSpacing) {
        return std::unexpected("The trace must be thinner than the spacing of the planes");
    }
    const double width_ratio = strip.Width / strip.PlaneSpacing;
    const double narrow_correction = width_ratio < 0.35 ? (0.35 - width_ratio) * (0.35 - width_ratio) : 0.0;
    const double effective_ratio = width_ratio - narrow_correction;
    return CrossSectionFigures{
        .CharacteristicImpedance =
            30.0 * std::numbers::pi / std::sqrt(relative_permittivity) / (effective_ratio + 0.441),
        .EffectivePermittivity = relative_permittivity,
        .FillingFactor = 1.0,
        .DcArea = strip.Width * strip.Thickness,
        .SkinFactor = 1.0 / strip.Width,
    };
}

std::optional<std::string> CheckMaterialsAndLength(const LineGeometry &geometry) {
    if (geometry.Materials.RelativePermittivity < 1.0) {
        return "The relative permittivity must be at least 1";
    }
    if (geometry.Materials.LossTangent < 0.0) {
        return "The loss tangent cannot be negative";
    }
    if (geometry.Materials.Conductivity <= 0.0) {
        return "The conductivity must be above zero";
    }
    if (geometry.Length <= 0.0) {
        return "The length must be above zero";
    }
    if (geometry.ReferenceFrequency <= 0.0) {
        return "The reference frequency must be above zero";
    }
    return std::nullopt;
}

} // namespace

/**
 * @brief   Works out the value and parameters of a TransmissionLine from the geometry of a real line.
 * @param[in] geometry  Cross-section, materials, length and the frequency the skin resistance is given at.
 * @return  Z0, the effective permittivity and the parameters: the delay \f$\ell\sqrt{\varepsilon_{\text{eff}}}/c\f$,
 *          the DC resistance \f$\ell/(\sigma A)\f$, the skin resistance at the reference frequency from the surface
 *          resistance \f$R_s = \sqrt{\pi f \mu_0/\sigma}\f$, the loss tangent the wave sees and no shunt conductance;
 *          or why the geometry is not a line, such as a shield smaller than its inner conductor.
 * @note    The figures are closed-form approximations, within a few percent in the usual ranges. The skin resistance
 *          assumes conductors thicker than the skin depth and smooth copper, so it is low for thin or rough copper.
 */
std::expected<LineElectrical, std::string> ComputeLineElectrical(const LineGeometry &geometry) {
    if (std::optional<std::string> error = CheckMaterialsAndLength(geometry)) {
        return std::unexpected(std::move(*error));
    }
    const double permittivity = geometry.Materials.RelativePermittivity;
    const FiguresOrError figures = std::visit(
        [permittivity](const auto &section) -> FiguresOrError {
            using Section = std::decay_t<decltype(section)>;
            if constexpr (std::is_same_v<Section, CoaxGeometry>) {
                return ComputeCoax(section, permittivity);
            } else if constexpr (std::is_same_v<Section, MicrostripGeometry>) {
                return ComputeMicrostrip(section, permittivity);
            } else {
                return ComputeStripline(section, permittivity);
            }
        },
        geometry.CrossSection);
    if (!figures) {
        return std::unexpected(figures.error());
    }

    const double conductivity = geometry.Materials.Conductivity;
    const double surface_resistance =
        std::sqrt(std::numbers::pi * geometry.ReferenceFrequency * VACUUM_PERMEABILITY / conductivity);
    return LineElectrical{
        .CharacteristicImpedance = figures->CharacteristicImpedance,
        .EffectivePermittivity = figures->EffectivePermittivity,
        .Parameters = {.Delay = geometry.Length * std::sqrt(figures->EffectivePermittivity) / SPEED_OF_LIGHT,
                       .DcResistance = geometry.Length / (conductivity * figures->DcArea),
                       .SkinResistance = geometry.Length * surface_resistance * figures->SkinFactor,
                       .DcConductance = 0.0,
                       .LossTangent = geometry.Materials.LossTangent * figures->FillingFactor,
                       .ReferenceFrequency = geometry.ReferenceFrequency},
    };
}

} // namespace Core

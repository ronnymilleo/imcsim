/**
 * @file    line_geometry.h
 * @brief   Works out the electrical figures of a transmission line from its cross-section, materials and length.
 */

#ifndef IMCSIM_LINE_GEOMETRY_H
#define IMCSIM_LINE_GEOMETRY_H

#include "circuit/components/transmission_line.h"
#include <expected>
#include <string>
#include <variant>

namespace Core {

/**
 * @struct  CoaxGeometry
 * @brief   A coaxial cable: a round inner conductor inside a round shield, filled with the dielectric.
 */
struct CoaxGeometry {
    double InnerDiameter = 0.9e-3;  ///< Diameter of the inner conductor, in meters.
    double OuterDiameter = 2.95e-3; ///< Inner diameter of the shield, in meters.
};

/**
 * @struct  MicrostripGeometry
 * @brief   A trace on top of a substrate over a ground plane, with air above it.
 */
struct MicrostripGeometry {
    double Width = 3.0e-3;    ///< Width of the trace, in meters.
    double Height = 1.6e-3;   ///< Thickness of the substrate between the trace and the plane, in meters.
    double Thickness = 35e-6; ///< Thickness of the copper of the trace, in meters.
};

/**
 * @struct  StriplineGeometry
 * @brief   A trace centered between two ground planes, buried in the dielectric.
 */
struct StriplineGeometry {
    double Width = 0.5e-3;        ///< Width of the trace, in meters.
    double PlaneSpacing = 1.0e-3; ///< Distance between the two planes, in meters.
    double Thickness = 35e-6;     ///< Thickness of the copper of the trace, in meters.
};

/**
 * @struct  LineMaterials
 * @brief   The dielectric and the conductor a line is made of.
 */
struct LineMaterials {
    double RelativePermittivity = 4.3; ///< \f$\varepsilon_r\f$ of the dielectric, FR-4 by default.
    double LossTangent = 0.02;         ///< \f$\tan\delta\f$ of the dielectric.
    double Conductivity = 5.8e7;       ///< Conductivity of the conductor, in S/m; copper by default.
};

/// The shape of a line across its length.
using LineCrossSection = std::variant<CoaxGeometry, MicrostripGeometry, StriplineGeometry>;

/**
 * @struct  LineGeometry
 * @brief   Everything that sets the electrical figures of a line: its cross-section, materials and length.
 */
struct LineGeometry {
    LineCrossSection CrossSection = MicrostripGeometry{};
    LineMaterials Materials;
    double Length = 0.1;             ///< Physical length, in meters.
    double ReferenceFrequency = 1e9; ///< Frequency the skin resistance is given at, in Hz.
};

/**
 * @struct  LineElectrical
 * @brief   What a geometry gives a TransmissionLine: its value Z0 and its parameters.
 */
struct LineElectrical {
    double CharacteristicImpedance = 0.0; ///< \f$Z_0\f$, the value of the part, in Ohms.
    double EffectivePermittivity = 0.0;   ///< \f$\varepsilon_{\text{eff}}\f$ the wave sees, which sets the delay.
    TransmissionLineParameters Parameters;
};

[[nodiscard]] std::expected<LineElectrical, std::string> ComputeLineElectrical(const LineGeometry &geometry);

} // namespace Core

#endif // IMCSIM_LINE_GEOMETRY_H

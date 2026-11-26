/**
 * @file    nodalys_unsupported_parts.cpp
 * @brief   The parts each analysis of Nodalys cannot simulate, with the reason shown to the user.
 */

#include "backends/nodalys/nodalys_unsupported_parts.h"

#include "circuit/components/logic_part.h"
#include <array>
#include <format>
#include <string_view>

namespace Core {

namespace {

/**
 * @struct  UnsupportedPart
 * @brief   A part one analysis of Nodalys cannot simulate, with the reason shown to the user.
 */
struct UnsupportedPart {
    AnalysisKind Analysis;
    ComponentType Type;
    std::string_view Reason;
};

/**
 * @struct  LatchingPart
 * @brief   A part whose latch Nodalys simulates in a transient, and that the analyses needing a smooth or frozen
 *          circuit refuse.
 */
struct LatchingPart {
    ComponentType Type;
    std::string_view Noun; ///< How the reasons name the part, such as "TRIAC".
};

constexpr auto LATCHING_PARTS = std::to_array<LatchingPart>({
    {.Type = ComponentType::THYRISTOR, .Noun = "thyristor"},
    {.Type = ComponentType::TRIAC, .Noun = "TRIAC"},
    {.Type = ComponentType::GTO, .Noun = "GTO"},
    {.Type = ComponentType::DIAC, .Noun = "DIAC"},
});

constexpr auto OTHER_UNSUPPORTED_PARTS = std::to_array<UnsupportedPart>({
    {.Analysis = AnalysisKind::HARMONIC_BALANCE,
     .Type = ComponentType::V_SWITCH,
     .Reason = "Nodalys harmonic balance does not support hysteretic switches"},
    {.Analysis = AnalysisKind::HARMONIC_BALANCE,
     .Type = ComponentType::RELAY,
     .Reason = "Nodalys harmonic balance does not support hysteretic relays"},
    {.Analysis = AnalysisKind::POLE_ZERO,
     .Type = ComponentType::TRANSMISSION_LINE,
     .Reason = "Pole-zero analysis does not support transmission lines, which have infinitely many poles and zeros"},
});

/**
 * @brief   Refuses logic gates and flip-flops in the analyses that need a small-signal or differentiable model of
 *          them.
 * @param[in,out] capabilities  Capabilities the refusals are added to.
 */
void RefuseLogicParts(BackendCapabilities &capabilities) {
    for (int index = 0; index <= static_cast<int>(LAST_COMPONENT_TYPE); ++index) {
        const auto type = static_cast<ComponentType>(index);
        if (!IsLogicPart(type)) {
            continue;
        }
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::SENSITIVITY][type] =
            "Nodalys cannot run a sensitivity analysis on logic gates and flip-flops";
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::AC_SENSITIVITY][type] =
            "Nodalys cannot run an AC sensitivity analysis on logic gates and flip-flops";
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][type] =
            "Pole-zero analysis is small-signal, and digital parts have no small-signal model";
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::S_PARAMETERS][type] =
            "S-parameters are small-signal, and digital parts have no small-signal RF model";
    }
}

/**
 * @brief   Refuses the latching parts in the harmonic balance, which needs a smooth circuit, and in the pole-zero
 *          analysis, whose frozen state through a latch has no roots the solver can place.
 * @param[in,out] capabilities  Capabilities the refusals are added to.
 */
void RefuseLatchingParts(BackendCapabilities &capabilities) {
    for (const LatchingPart &part : LATCHING_PARTS) {
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::HARMONIC_BALANCE][part.Type] =
            std::format("Nodalys harmonic balance does not support the latching {}", part.Noun);
        capabilities.UnsupportedPartsByAnalysis[AnalysisKind::POLE_ZERO][part.Type] =
            std::format("Pole-zero analysis is small-signal, and a {} blocks nearly open or conducts as a switch, "
                        "whichever the circuit last drove, so its frozen state has no roots the solver can place",
                        part.Noun);
    }
}

} // namespace

/**
 * @brief   Adds to the capabilities of Nodalys the parts that some of its analyses cannot simulate, each with the
 *          reason shown to the user: logic parts in the small-signal and sensitivity analyses, latching parts in the
 *          harmonic balance and the pole-zero analysis, and a few others.
 * @param[in,out] capabilities  Capabilities the refusals are added to.
 */
void AddNodalysUnsupportedParts(BackendCapabilities &capabilities) {
    RefuseLogicParts(capabilities);
    RefuseLatchingParts(capabilities);
    for (const UnsupportedPart &part : OTHER_UNSUPPORTED_PARTS) {
        capabilities.UnsupportedPartsByAnalysis[part.Analysis][part.Type] = part.Reason;
    }
}

} // namespace Core

/**
 * @file    mode_input.cpp
 * @brief   What the mouse and the keys do in each mode of the editor once the schematic is drawn.
 */

#include "windows/mode_input.h"

#include "base/theme.h"
#include "imgui.h"
#include "schematic/placement_editing.h"
#include "schematic/probing.h"
#include "schematic/schematic_drawing.h"
#include "schematic/selection.h"
#include <format>
#include <string>
#include <utility>

namespace GUI {

namespace {

constexpr float WIRE_CURSOR_HALF_SIZE = 4.0f;

/**
 * @brief   What the Probe tool would measure under the cursor: the current of a part, or the voltage of a wire.
 */
std::optional<MeasurementTarget> FindTargetAt(const ModeInput &input) {
    Schematic &schematic = input.Blocks.GetShown();
    return FindMeasurementTarget(schematic.GetElements(), schematic.GetWires(), schematic.GetConnectivity(),
                                 input.View.ToWorld(ImGui::GetIO().MousePos), WIRE_PICK_DISTANCE / input.Zoom);
}

/**
 * @brief   Turns or mirrors the pasted group by the keys pressed this frame.
 * @return  True when Esc cancels the paste.
 */
bool HandlePastingKeys(PastedGroup &group) {
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        TransformGroup(group, {.Type = PointTransform::Kind::QUARTER_TURN, .Delta = {}, .Pivot = {}});
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        const bool top_to_bottom = ImGui::GetIO().KeyShift;
        TransformGroup(group, {.Type = top_to_bottom ? PointTransform::Kind::MIRROR_TOP_BOTTOM
                                                     : PointTransform::Kind::MIRROR_LEFT_RIGHT,
                               .Delta = {},
                               .Pivot = {}});
    }
    return ImGui::IsKeyPressed(ImGuiKey_Escape);
}

/**
 * @brief   Draws the wire in progress with its bend, or the cursor of the wire tool before the first click.
 */
void DrawWirePreview(const WiringMode &mode, const ModeInput &input, const GridPoint cursor) {
    if (!mode.Start) {
        const ImVec2 cursor_on_screen = input.View.ToScreen(ToVec2(cursor));
        const ImVec2 half_size = {WIRE_CURSOR_HALF_SIZE, WIRE_CURSOR_HALF_SIZE};
        ImGui::GetWindowDrawList()->AddRect(cursor_on_screen - half_size, cursor_on_screen + half_size,
                                            GetPreviewColor());
    } else {
        const GridPoint corner = FindWireCorner(*mode.Start, cursor, input.IsWireVerticalFirst);
        UIWire(*mode.Start, corner).Draw(input.Canvas, input.View, GetPreviewColor());
        UIWire(corner, cursor).Draw(input.Canvas, input.View, GetPreviewColor());
    }
}

} // namespace

/**
 * @brief   Selecting handles its clicks before the schematic is drawn (EditorWindow::HandleSelection()), so a dragged
 *          part is drawn where the cursor is; here, hovering a wire or a part shows its value on display.
 */
std::optional<EditorMode> HandleMode(SelectingMode &mode, const ModeInput &input) {
    if (!input.IsHovered || mode.Drag || !input.Overlays.GetDisplayed()) {
        return std::nullopt;
    }
    if (const std::optional<MeasurementTarget> target = FindTargetAt(input)) {
        input.Overlays.DrawHoveredValue(*target, input.Blocks.GetShown());
    }
    return std::nullopt;
}

/**
 * @brief   Placement mode: R rotates, M mirrors left to right and Shift+M top to bottom, Esc or right click cancels,
 *          left click places and keeps the mode active so several components can be placed in a row.
 * @details A label or a pin marker needs a name next, so placing one ends the mode and opens its popover on the name.
 */
std::optional<EditorMode> HandleMode(PlacingMode &mode, const ModeInput &input) {
    // Keyboard shortcuts only apply while the editor has focus, so typing elsewhere does not trigger them
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            TurnPlacement(mode);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) {
            MirrorPlacement(mode, ImGui::GetIO().KeyShift);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            return SelectingMode{};
        }
    }
    if (!input.IsHovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    Schematic &schematic = input.Blocks.GetShown();
    const GridPoint position = Snap(input.View.ToWorld(ImGui::GetIO().MousePos));
    std::unique_ptr<UIElement> preview = MakePlacementPreview(mode, position, schematic, input.Root.GetSubcircuits());
    if (!preview) {
        return SelectingMode{};
    }
    preview->Draw(input.Canvas, input.View, GetPreviewColor(), input.UserPreferences.Symbols);
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    schematic.AddElement(std::move(preview));
    if (mode.Type != Core::ComponentType::NET_LABEL && !Core::IsSubcircuitPin(mode.Type)) {
        return std::nullopt;
    }
    schematic.SelectElement(schematic.GetElements().size() - 1);
    input.Part.FocusNetName();
    input.Popover.Open();
    return SelectingMode{};
}

/**
 * @brief   Wire mode: the first click starts a wire, each following click adds an L-shaped bend up to the cursor and
 *          continues from there. F flips the bend, and the wire ends on a terminal, on a second click at the same
 *          point, or with Esc or right click. Esc or right click with no wire in progress leaves the mode.
 */
std::optional<EditorMode> HandleMode(WiringMode &mode, const ModeInput &input) {
    if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            input.IsWireVerticalFirst = !input.IsWireVerticalFirst;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            return StopWire(mode) ? std::optional<EditorMode>(SelectingMode{}) : std::nullopt;
        }
    }
    if (!input.IsHovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return StopWire(mode) ? std::optional<EditorMode>(SelectingMode{}) : std::nullopt;
    }

    const GridPoint cursor = Snap(input.View.ToWorld(ImGui::GetIO().MousePos));
    DrawWirePreview(mode, input, cursor);
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    Schematic &schematic = input.Blocks.GetShown();
    for (const UIWire &wire : ClickWire(mode, cursor, input.IsWireVerticalFirst, schematic.IsTerminal(cursor))) {
        schematic.AddWire(wire.GetStart(), wire.GetEnd());
    }
    return std::nullopt;
}

/**
 * @brief   Probe mode: the item under the cursor is outlined with what it measures, a node voltage on a wire or a
 *          current on a part, and a left click adds it to the plots or removes it. Esc or right click leaves the mode.
 */
std::optional<EditorMode> HandleMode(ProbingMode & /*mode*/, const ModeInput &input) {
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        return SelectingMode{};
    }
    if (!input.IsHovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    Schematic &schematic = input.Blocks.GetShown();
    const std::optional<MeasurementTarget> target = FindTargetAt(input);
    if (!target) {
        ImGui::SetTooltip("Click a wire for its voltage, or a part for its current");
        return std::nullopt;
    }
    const bool measured =
        target->Node ? schematic.IsVoltageMeasured(*target->Node) : schematic.IsCurrentMeasured(target->Current);
    ImGui::GetWindowDrawList()->AddCircle(ImGui::GetIO().MousePos, MEASUREMENT_MARKER_RADIUS * 2.0f,
                                          GetHighlightColor(), 0, LINE_THICKNESS);
    const std::optional<DisplayedState> &displayed = input.Overlays.GetDisplayed();
    ImGui::SetTooltip("%s", std::format("{}\nClick to {} the plots",
                                        DescribeMeasurement(*target, schematic.GetConnectivity(),
                                                            displayed ? &displayed->Point : nullptr),
                                        measured ? "remove it from" : "add it to")
                                .c_str());
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    if (target->Node) {
        schematic.SetVoltageMeasured(*target->Node, !measured);
    } else {
        schematic.SetCurrentMeasured(target->Current, !measured);
    }
    return std::nullopt;
}

/**
 * @brief   Paste mode: the pasted group follows the cursor, half see-through, R rotates it, M mirrors it left to right
 *          and Shift+M top to bottom, Esc or right click cancels, and a left click places it and selects what was
 *          placed. Parts get the next free names of the schematic.
 */
std::optional<EditorMode> HandleMode(PastingMode &mode, const ModeInput &input) {
    PastedGroup &group = mode.Group;
    if (ImGui::IsWindowFocused() && HandlePastingKeys(group)) {
        return SelectingMode{};
    }
    if (!input.IsHovered) {
        return std::nullopt;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return SelectingMode{};
    }

    MovePivotTo(group, Snap(input.View.ToWorld(ImGui::GetIO().MousePos)));
    for (const UIWire &wire : group.Wires) {
        wire.Draw(input.Canvas, input.View, GetPreviewColor());
    }
    for (const auto &element : group.Elements) {
        element->Draw(input.Canvas, input.View, GetPreviewColor(), input.UserPreferences.Symbols);
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return std::nullopt;
    }
    if (const std::optional<std::string> refusal = PlacePastedGroup(input.Root, input.Blocks, group)) {
        input.Files.QueueMessages("Could not paste inside the block", {*refusal});
    }
    return SelectingMode{};
}

} // namespace GUI

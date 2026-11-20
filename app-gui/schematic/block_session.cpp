/**
 * @file    block_session.cpp
 * @brief   The blocks the editor has open on the way down from the schematic, and the settings of the one shown.
 */

#include "schematic/block_session.h"

#include "schematic/block_nesting.h"
#include "schematic/free_name.h"
#include "schematic/subcircuit_pins.h"
#include <algorithm>
#include <functional>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace GUI {

namespace {

/// The name a new block is offered first, numbered as "block_2" and so on when blocks have it.
constexpr std::string_view NEW_BLOCK_NAME = "block";

/**
 * @brief   One pin per marker the selection offers, named as the marker suggests.
 */
std::vector<SavedSubcircuitPin> PinsFromCandidates(const std::vector<SubcircuitPinCandidate> &candidates) {
    std::vector<SavedSubcircuitPin> pins;
    for (const SubcircuitPinCandidate &candidate : candidates) {
        pins.push_back({candidate.SuggestedName, candidate.Point});
    }
    return pins;
}

/**
 * @brief   Whether applying an open block would change the document: its contents were edited, or its settings differ
 *          from its definition there.
 * @details A block opened and closed without a change leaves the document unmodified, so this is false then.
 */
bool IsBlockChanged(const OpenBlock &block, const std::vector<SavedSubcircuit> &definitions) {
    const BlockSettings &settings = block.Settings;
    const auto definition = std::ranges::find(definitions, settings.Name, &SavedSubcircuit::Name);
    return block.Content->IsModified() || definition == definitions.end() || definition->Pins != settings.Pins
        || definition->Parameters != settings.Parameters;
}

/**
 * @brief   Applies a change the document took to the copies of it that some open blocks hold.
 * @details Every copy holds the same blocks as the document, so the change the document took applies to them alike,
 *          and a refusal there could only repeat one the document already gave; the result is not looked at.
 */
void PropagateToOpenBlocks(const std::span<OpenBlock> blocks,
                           const std::function<std::expected<void, std::string>(Schematic &)> &update) {
    for (OpenBlock &block : blocks) {
        static_cast<void>(update(*block.Content));
    }
}

} // namespace

/**
 * @brief   Starts a session on the document, with no block open.
 * @param[in] root  Schematic that blocks are opened from and applied to; it must outlive the session.
 */
BlockSession::BlockSession(Schematic &root) : m_Root(root) {
}

/**
 * @brief   Whether a block is open, so the editor shows its contents instead of the document.
 */
bool BlockSession::IsOpen() const {
    return !m_OpenBlocks.empty();
}

/**
 * @brief   The blocks open, from the outermost to the one shown.
 */
const std::vector<OpenBlock> &BlockSession::GetOpenBlocks() const {
    return m_OpenBlocks;
}

/**
 * @brief   The schematic on show: the contents of the innermost open block, or the document.
 */
Schematic &BlockSession::GetShown() {
    return IsOpen() ? *m_OpenBlocks.back().Content : m_Root;
}

const Schematic &BlockSession::GetShown() const {
    return IsOpen() ? *m_OpenBlocks.back().Content : m_Root;
}

/**
 * @brief   Whether a block can be placed where the editor is: never inside itself, nor inside a block it contains, at
 *          any depth.
 */
bool BlockSession::CanPlaceBlock(const std::string_view name) const {
    return std::ranges::none_of(m_OpenBlocks, [&](const OpenBlock &block) {
        return ContainsBlock(m_Root.GetSubcircuits(), name, block.Settings.Name);
    });
}

/**
 * @brief   The blocks of the document the part picker offers where the editor is, which CanPlaceBlock() allows.
 */
std::vector<const SavedSubcircuit *> BlockSession::ListPlaceableBlocks() const {
    std::vector<const SavedSubcircuit *> blocks;
    for (const SavedSubcircuit &definition : m_Root.GetSubcircuits()) {
        if (CanPlaceBlock(definition.Name)) {
            blocks.push_back(&definition);
        }
    }
    return blocks;
}

/**
 * @brief   The contents of every open block take the blocks and models of the document again, after it gained some.
 */
void BlockSession::SyncDefinitions() {
    for (OpenBlock &block : m_OpenBlocks) {
        block.Content->CopyDefinitionsFrom(m_Root, block.Settings.Name);
    }
}

/**
 * @brief   The name of the block an element places.
 * @param[in] element  Element to look at, or null.
 * @return  The definition name, or none when the element is null or is not a block instance.
 */
std::optional<std::string> BlockSession::FindBlockName(const UIElement *element) {
    if (element == nullptr || element->GetComponent().GetType() != Core::ComponentType::SUBCIRCUIT) {
        return std::nullopt;
    }
    return static_cast<const Core::SubcircuitInstance &>(element->GetComponent()).GetDefinitionName();
}

/**
 * @brief   The definition of a block in the document.
 * @return  The definition, or null when the document has none by that name.
 */
const SavedSubcircuit *BlockSession::FindDefinition(const std::string &name) const {
    const std::vector<SavedSubcircuit> &definitions = m_Root.GetSubcircuits();
    const auto definition = std::ranges::find(definitions, name, &SavedSubcircuit::Name);
    return definition != definitions.end() ? &*definition : nullptr;
}

/**
 * @brief   Prepares the dialog that makes a block from the selection: a free name, and the pins the selection offers.
 * @note    It does nothing while a block is open, whose contents cannot be turned into a block of their own here.
 */
void BlockSession::RequestCreate() {
    if (IsOpen()) {
        return;
    }
    const auto candidates = m_Root.GetSubcircuitPinCandidates();
    m_NewBlock.Name = FindFreeName(NEW_BLOCK_NAME, [this](const std::string_view name) {
        return std::ranges::contains(m_Root.GetSubcircuits(), name, &SavedSubcircuit::Name);
    });
    m_NewBlock.Pins = candidates ? PinsFromCandidates(*candidates) : std::vector<SavedSubcircuitPin>{};
    m_NewBlock.Parameters.clear();
    m_Error = candidates ? std::string{} : candidates.error();
    m_IsDialogRequested = true;
}

/**
 * @brief   Opens a block of the document one level down: the way back up keeps the path, and the block it leaves keeps
 *          its settings until the editor comes back to it.
 * @param[in] name  A block of the document.
 * @return  Nothing once it opened; the reason otherwise, with nothing opened, which ReportError() can show.
 * @note    The block must have drawn contents, so a SPICE block is not opened here.
 */
std::expected<void, std::string> BlockSession::Open(const std::string &name) {
    auto content = m_Root.OpenSubcircuit(name);
    if (!content) {
        return std::unexpected(content.error());
    }
    const SavedSubcircuit *definition = FindDefinition(name);
    m_OpenBlocks.push_back(
        {.Content = std::move(*content),
         .Settings = {.Name = name, .Pins = definition->Pins, .Parameters = definition->Parameters}});
    return {};
}

/**
 * @brief   Applies the innermost open block to the document and closes it.
 * @details It is one undo step of the document, and the blocks still open take it too, since they may hold instances
 *          of it. When it cannot be applied it stays open.
 * @return  Nothing once it closed, so the editor shows the level above it; the reason otherwise, which ReportError()
 *          can show: no block is open, or the block cannot be applied and stays open.
 */
std::expected<void, std::string> BlockSession::ApplyAndClose() {
    if (!IsOpen()) {
        return std::unexpected("No block is open");
    }
    if (auto applied = ApplyInnermost(); !applied) {
        return applied;
    }
    LeaveInnermost();
    return {};
}

/**
 * @brief   Closes the innermost open block and drops the changes made in it.
 * @return  Nothing once it closed, so the editor shows the level above it; the reason when no block is open.
 */
std::expected<void, std::string> BlockSession::DiscardAndClose() {
    if (!IsOpen()) {
        return std::unexpected("No block is open");
    }
    LeaveInnermost();
    return {};
}

/**
 * @brief   Changes the settings of a block kept as SPICE text, in the document and in the open blocks that hold a copy.
 * @return  Nothing, or why the settings were refused; the document is left as it was then.
 */
std::expected<void, std::string>
BlockSession::ApplySpiceUpdate(const std::string &name,
                               const std::vector<SavedSubcircuitPin> &pins,
                               const std::vector<Core::SubcircuitParameter> &parameters) {
    const auto updated = m_Root.UpdateSpiceSubcircuit(name, pins, parameters);
    if (!updated) {
        return updated;
    }
    m_Root.CommitUndoStep();
    // The open blocks hold copies of it, whose instances follow it as the document's do
    PropagateToOpenBlocks(m_OpenBlocks,
                          [&](Schematic &copy) { return copy.UpdateSpiceSubcircuit(name, pins, parameters); });
    return {};
}

/**
 * @brief   Brings the pins of the settings in step with the pin markers of the open block: pins whose marker is gone
 *          drop out, the order of the others stays, and new markers join at the end.
 * @return  Nothing once the pins follow the markers; the reason when the markers cannot be read, with the pins
 *          unchanged, which ReportError() can show.
 */
std::expected<void, std::string> BlockSession::SyncPins() {
    auto markers = m_OpenBlocks.back().Content->GetSubcircuitPinMarkers();
    if (!markers) {
        return std::unexpected(markers.error());
    }
    BlockSettings &settings = m_OpenBlocks.back().Settings;
    std::vector<SavedSubcircuitPin> synchronized;
    std::set<std::string> added;
    for (const SavedSubcircuitPin &pin : settings.Pins) {
        const auto marker = std::ranges::find(*markers, pin.Name, &Schematic::SubcircuitPinCandidate::SuggestedName);
        if (marker != markers->end()) {
            synchronized.push_back({pin.Name, marker->Point});
            added.insert(pin.Name);
        }
    }
    for (const auto &marker : *markers) {
        if (added.insert(marker.SuggestedName).second) {
            synchronized.push_back({marker.SuggestedName, marker.Point});
        }
    }
    settings.Pins = std::move(synchronized);
    return {};
}

/**
 * @brief   Names the pin markers of the open block after the pins of the settings, which sit on the same points.
 * @return  Nothing once the markers are named; the reason when a marker has no pin.
 */
std::expected<void, std::string> BlockSession::ApplyPinNames() {
    Schematic &content = *m_OpenBlocks.back().Content;
    const auto named = NameMarkersAfterPins(content.GetElements(), m_OpenBlocks.back().Settings.Pins);
    if (!named) {
        return std::unexpected(named.error());
    }
    if (*named) {
        content.MarkModified();
    }
    return {};
}

/**
 * @brief   Makes a block of the selection of the document from the settings, as one undo step.
 * @return  Nothing once it was made; the reason otherwise, with the document unchanged.
 */
std::expected<void, std::string> BlockSession::CreateBlock() {
    if (auto created = m_Root.CreateSubcircuitFromSelection(m_NewBlock.Name, m_NewBlock.Pins, m_NewBlock.Parameters);
        !created) {
        return created;
    }
    m_Root.CommitUndoStep();
    return {};
}

/**
 * @brief   Takes the pins and parameters of the open block back from its definition in the document, which is what the
 *          settings dialog does when it is cancelled.
 */
void BlockSession::RestoreSettings() {
    BlockSettings &settings = m_OpenBlocks.back().Settings;
    const SavedSubcircuit *definition = FindDefinition(settings.Name);
    settings.Pins = definition->Pins;
    settings.Parameters = definition->Parameters;
}

std::string &BlockSession::GetName() {
    return GetShownSettings().Name;
}

std::vector<SavedSubcircuitPin> &BlockSession::GetPins() {
    return GetShownSettings().Pins;
}

std::vector<Core::SubcircuitParameter> &BlockSession::GetParameters() {
    return GetShownSettings().Parameters;
}

const std::string &BlockSession::GetError() const {
    return m_Error;
}

/**
 * @brief   Shows a reason in the settings dialog, such as why its button could not apply the settings.
 * @param[in] reason  The reason, as the methods of the session return it.
 */
void BlockSession::ShowError(std::string reason) {
    m_Error = std::move(reason);
}

/**
 * @brief   Takes the reason off the settings dialog, once what failed has worked.
 */
void BlockSession::ClearError() {
    m_Error.clear();
}

/**
 * @brief   Shows why something the editor asked for failed, in the settings dialog, which opens on the next frame.
 * @param[in] reason  The reason, as Open(), ApplyAndClose() or SyncPins() return it.
 */
void BlockSession::ReportError(std::string reason) {
    ShowError(std::move(reason));
    m_IsDialogRequested = true;
}

/**
 * @brief   Asks for the settings dialog to open on the next frame.
 */
void BlockSession::RequestDialog() {
    m_IsDialogRequested = true;
}

/**
 * @brief   Whether the dialog was asked for since the last call, which forgets the request.
 */
bool BlockSession::TakeDialogRequest() {
    return std::exchange(m_IsDialogRequested, false);
}

/**
 * @brief   Applies the innermost open block to the document as one undo step, and to the blocks open around it, which
 *          may hold instances of it.
 * @return  Nothing once applied, or when nothing changed; the reason otherwise, with the document unchanged.
 */
std::expected<void, std::string> BlockSession::ApplyInnermost() {
    if (auto synced = SyncPins(); !synced) {
        return synced;
    }
    const OpenBlock &block = m_OpenBlocks.back();
    if (!IsBlockChanged(block, m_Root.GetSubcircuits())) {
        return {};
    }
    const BlockSettings &settings = block.Settings;
    const auto update = [&](Schematic &target) {
        return target.UpdateSubcircuit(settings.Name, *block.Content, settings.Pins, settings.Parameters);
    };
    if (auto updated = update(m_Root); !updated) {
        return updated;
    }
    m_Root.CommitUndoStep();
    PropagateToOpenBlocks(std::span(m_OpenBlocks).first(m_OpenBlocks.size() - 1), update);
    return {};
}

/**
 * @brief   Drops the innermost open block, so the level above it is shown.
 * @details Back at the schematic the dialog has no pins or parameters to offer until a block is made, and the name
 *          stays the one last shown.
 */
void BlockSession::LeaveInnermost() {
    std::string closed_name = std::move(m_OpenBlocks.back().Settings.Name);
    m_OpenBlocks.pop_back();
    if (!IsOpen()) {
        m_NewBlock = {.Name = std::move(closed_name), .Pins = {}, .Parameters = {}};
    }
}

/**
 * @brief   The settings the dialog edits: those of the innermost open block, or of the block to make from the
 * selection.
 */
BlockSettings &BlockSession::GetShownSettings() {
    return IsOpen() ? m_OpenBlocks.back().Settings : m_NewBlock;
}

} // namespace GUI

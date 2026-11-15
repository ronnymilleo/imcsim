/**
 * @file    block_session.cpp
 * @brief   The blocks the editor has open on the way down from the schematic, and the settings of the one shown.
 */

#include "schematic/block_session.h"

#include "circuit/components/net_label.h"
#include "schematic/block_nesting.h"
#include <algorithm>
#include <set>
#include <utility>

namespace GUI {

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
    return std::ranges::none_of(
        m_OpenBlocks, [&](const OpenBlock &block) { return ContainsBlock(m_Root.GetSubcircuits(), name, block.Name); });
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
        block.Content->CopyDefinitionsFrom(m_Root, block.Name);
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
    auto candidates = m_Root.GetSubcircuitPinCandidates();
    m_Name = "block";
    while (std::ranges::find(m_Root.GetSubcircuits(), m_Name, &SavedSubcircuit::Name) !=
           m_Root.GetSubcircuits().end()) {
        m_Name += "_2";
    }
    m_Pins.clear();
    m_Parameters.clear();
    m_Error.clear();
    if (candidates) {
        for (const auto &candidate : *candidates) {
            m_Pins.push_back({candidate.SuggestedName, candidate.Point});
        }
    } else {
        m_Error = candidates.error();
    }
    m_IsDialogRequested = true;
}

/**
 * @brief   Opens a block of the document one level down: the way back up keeps the path, and the block it leaves keeps
 *          its pins and parameters until the editor comes back to it.
 * @param[in] name  A block of the document.
 * @return  True when it opened; false leaves the reason in GetError() and asks for the dialog.
 * @note    The block must have drawn contents, so a SPICE block is not opened here.
 */
bool BlockSession::Open(const std::string &name) {
    auto content = m_Root.OpenSubcircuit(name);
    if (!content) {
        m_Error = content.error();
        m_IsDialogRequested = true;
        return false;
    }
    const SavedSubcircuit *definition = FindDefinition(name);
    if (IsOpen()) {
        m_OpenBlocks.back().Pins = std::move(m_Pins);
        m_OpenBlocks.back().Parameters = std::move(m_Parameters);
    }
    m_OpenBlocks.push_back({.Name = name, .Content = std::move(*content), .Pins = {}, .Parameters = {}});
    m_Name = name;
    m_Pins = definition->Pins;
    m_Parameters = definition->Parameters;
    return true;
}

/**
 * @brief   Closes the innermost open block, applying it to the document or dropping its changes.
 * @details Applied, it is one undo step of the document, and the blocks still open take it too, since they may hold
 *          instances of it. When it cannot be applied it stays open with the reason.
 * @param[in] apply  Whether to apply the contents and the settings, or drop them.
 * @return  True when a block closed, so the editor shows the level above it.
 */
bool BlockSession::Close(const bool apply) {
    if (!IsOpen()) {
        return false;
    }
    const OpenBlock &block = m_OpenBlocks.back();
    if (apply) {
        if (!SyncPins()) {
            m_IsDialogRequested = true;
            return false;
        }
        // A block opened and closed without a change leaves the document unmodified
        const auto definition = std::ranges::find(m_Root.GetSubcircuits(), block.Name, &SavedSubcircuit::Name);
        const bool is_changed = block.Content->IsModified() || definition == m_Root.GetSubcircuits().end() ||
                                definition->Pins != m_Pins || definition->Parameters != m_Parameters;
        if (is_changed) {
            auto updated = m_Root.UpdateSubcircuit(block.Name, *block.Content, m_Pins, m_Parameters);
            if (!updated) {
                m_Error = updated.error();
                m_IsDialogRequested = true;
                return false;
            }
            m_Root.CommitUndoStep();
            // Every copy holds the same blocks as the document, so they take it as the document did
            for (std::size_t level = 0; level + 1 < m_OpenBlocks.size(); ++level) {
                static_cast<void>(
                    m_OpenBlocks[level].Content->UpdateSubcircuit(block.Name, *block.Content, m_Pins, m_Parameters));
            }
        }
    }
    m_OpenBlocks.pop_back();
    if (IsOpen()) {
        OpenBlock &outer = m_OpenBlocks.back();
        m_Name = outer.Name;
        m_Pins = std::move(outer.Pins);
        m_Parameters = std::move(outer.Parameters);
    } else {
        m_Pins.clear();
        m_Parameters.clear();
    }
    return true;
}

/**
 * @brief   Changes the settings of a block kept as SPICE text, in the document and in the open blocks that hold a copy.
 * @return  Nothing, or why the settings were refused; the document is left as it was then.
 */
std::expected<void, std::string>
BlockSession::ApplySpiceUpdate(const std::string &name, const std::vector<SavedSubcircuitPin> &pins,
                               const std::vector<Core::SubcircuitParameter> &parameters) {
    const auto updated = m_Root.UpdateSpiceSubcircuit(name, pins, parameters);
    if (!updated) {
        return updated;
    }
    m_Root.CommitUndoStep();
    // The open blocks hold copies of it, whose instances follow it as the document's do
    for (OpenBlock &block : m_OpenBlocks) {
        static_cast<void>(block.Content->UpdateSpiceSubcircuit(name, pins, parameters));
    }
    return {};
}

/**
 * @brief   Brings the pins of the settings in step with the pin markers of the open block: pins whose marker is gone
 *          drop out, the order of the others stays, and new markers join at the end.
 * @return  False, with the reason in GetError(), when the markers cannot be read.
 */
bool BlockSession::SyncPins() {
    auto markers = m_OpenBlocks.back().Content->GetSubcircuitPinMarkers();
    if (!markers) {
        m_Error = markers.error();
        return false;
    }
    std::vector<SavedSubcircuitPin> synchronized;
    std::set<std::string> added;
    for (const SavedSubcircuitPin &pin : m_Pins) {
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
    m_Pins = std::move(synchronized);
    return true;
}

/**
 * @brief   Names the pin markers of the open block after the pins of the settings, which sit on the same points.
 * @return  False, with the reason in GetError(), when a marker has no pin.
 */
bool BlockSession::ApplyPinNames() {
    Schematic &content = *m_OpenBlocks.back().Content;
    for (auto &element : content.GetElements()) {
        if (!Core::IsSubcircuitPin(element->GetComponent().GetType())) {
            continue;
        }
        const GridPoint point = element->GetTerminals().front();
        const auto pin = std::ranges::find(m_Pins, point, &SavedSubcircuitPin::Point);
        if (pin == m_Pins.end()) {
            m_Error = "Every pin marker needs a block pin name";
            return false;
        }
        auto &marker = static_cast<Core::NetLabel &>(element->GetComponent());
        if (marker.GetNet() != pin->Name) {
            marker.SetNet(pin->Name);
            content.MarkModified();
        }
    }
    return true;
}

/**
 * @brief   Makes a block of the selection of the document from the settings, as one undo step.
 * @return  True when it was made; false leaves the reason in GetError().
 */
bool BlockSession::CreateBlock() {
    auto created = m_Root.CreateSubcircuitFromSelection(m_Name, m_Pins, m_Parameters);
    if (!created) {
        m_Error = created.error();
        return false;
    }
    m_Root.CommitUndoStep();
    m_Error.clear();
    return true;
}

/**
 * @brief   Takes the pins and parameters of the open block back from its definition in the document, which is what the
 *          settings dialog does when it is cancelled.
 */
void BlockSession::RestoreSettings() {
    const SavedSubcircuit *definition = FindDefinition(m_OpenBlocks.back().Name);
    m_Pins = definition->Pins;
    m_Parameters = definition->Parameters;
}

std::string &BlockSession::GetName() {
    return m_Name;
}

std::vector<SavedSubcircuitPin> &BlockSession::GetPins() {
    return m_Pins;
}

std::vector<Core::SubcircuitParameter> &BlockSession::GetParameters() {
    return m_Parameters;
}

std::string &BlockSession::GetError() {
    return m_Error;
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

} // namespace GUI

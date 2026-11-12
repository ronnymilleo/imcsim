/**
 * @file    block_dialog.h
 * @brief   The dialog that makes a block or sets the pins and parameters of the one open, and the bar that shows the
 * way back from a block.
 */

#ifndef IMCSIM_BLOCK_DIALOG_H
#define IMCSIM_BLOCK_DIALOG_H

#include "imgui.h"
#include "schematic/block_session.h"
#include <cstddef>
#include <optional>

namespace GUI {

// Dialog
bool DrawBlockDialog(BlockSession &session);

// Path
std::optional<std::size_t> DrawBlockBar(const BlockSession &session, ImVec2 origin);

} // namespace GUI

#endif // IMCSIM_BLOCK_DIALOG_H

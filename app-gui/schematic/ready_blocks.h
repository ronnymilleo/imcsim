/**
 * @file    ready_blocks.h
 * @brief   The logic blocks that come with imcsim, such as a counter or a multiplexer, built from its gates and
 *          flip-flops.
 */

#ifndef IMCSIM_READY_BLOCKS_H
#define IMCSIM_READY_BLOCKS_H

#include "schematic/clipboard.h"
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GUI {

/**
 * @struct  ReadyBlock
 * @brief   A block that comes with imcsim, placed from the part search like a part.
 * @details FileName names its file in app-gui/ready_blocks, which holds one instance of the block and its definition.
 *          Placed, the definition joins the schematic as any block does, so it can be opened and changed there.
 */
struct ReadyBlock {
    std::string_view Title;
    /// The name of its definition, which the search finds too, so "mux" finds the multiplexer.
    std::string_view Name;
    std::string_view Description;
    std::string_view FileName;
};

// Listing
std::span<const ReadyBlock> GetReadyBlocks();
std::vector<const ReadyBlock *> FindReadyBlocks(std::string_view query);

// Placing
std::expected<PastedGroup, std::string> MakeReadyBlock(const ReadyBlock &block);

} // namespace GUI

#endif // IMCSIM_READY_BLOCKS_H

/**
 * @file    ready_blocks.cpp
 * @brief   The logic blocks that come with imcsim, such as a counter or a multiplexer, built from its gates and
 *          flip-flops.
 */

#include "document/ready_blocks.h"

#include "editing/element_factory.h"
#include "ready_block_files.h"
#include <algorithm>
#include <array>
#include <format>

namespace GUI {

namespace {

constexpr auto READY_BLOCKS = std::to_array<ReadyBlock>({
    {
        .Title = "4-bit counter",
        .Name = "COUNTER_4BIT",
        .Description = "A ripple counter of four D flip-flops: Q3 Q2 Q1 Q0 count the rising edges of CLK in binary, "
                       "from 0 to 15 and over again. RESET high clears it. Its logic level is 5 V.",
        .FileName = "counter_4bit.imcsim",
    },
    {
        .Title = "4-bit shift register",
        .Name = "SHIFT_REGISTER_4BIT",
        .Description = "Four D flip-flops in a row: each rising edge of CLK moves IN into Q0 and each Q on to the "
                       "next. RESET high clears it. Its logic level is 5 V.",
        .FileName = "shift_register_4bit.imcsim",
    },
    {
        .Title = "4-to-1 multiplexer",
        .Name = "MUX_4TO1",
        .Description = "Y follows the input among D0 to D3 that S1 S0 select in binary. Its logic level is 5 V.",
        .FileName = "mux_4to1.imcsim",
    },
    {
        .Title = "2-to-4 decoder",
        .Name = "DECODER_2TO4",
        .Description = "With EN high, the output among Y0 to Y3 that A1 A0 select in binary is high and the others "
                       "low; with EN low, all of them are low. Its logic level is 5 V.",
        .FileName = "decoder_2to4.imcsim",
    },
});

} // namespace

/**
 * @brief   Lists the blocks that come with imcsim.
 * @return  Every ready block, in the order the part search shows them.
 */
std::span<const ReadyBlock> GetReadyBlocks() {
    return READY_BLOCKS;
}

/**
 * @brief   Finds the ready blocks whose titles or definition names hold what was typed in the part search.
 * @param[in] query  What was typed; case, spaces and dashes do not count.
 * @return  The blocks that match, in list order; every one for an empty query.
 */
std::vector<const ReadyBlock *> FindReadyBlocks(const std::string_view query) {
    const std::string normalized_query = NormalizeSearchText(query);
    std::vector<const ReadyBlock *> found;
    for (const ReadyBlock &block : READY_BLOCKS) {
        if (NormalizeSearchText(block.Title).contains(normalized_query) ||
            NormalizeSearchText(block.Name).contains(normalized_query)) {
            found.push_back(&block);
        }
    }
    return found;
}

/**
 * @brief   Reads a ready block as a group to place, which follows the cursor until a click puts it down.
 * @param[in] block  One of GetReadyBlocks().
 * @return  One instance of the block with its definition, which joins the schematic when it is placed, as a pasted
 *          block does; the reason otherwise, which only a broken build can give.
 */
std::expected<PastedGroup, std::string> MakeReadyBlock(const ReadyBlock &block) {
    const auto file =
        std::ranges::find(READY_BLOCK_FILES, block.FileName, &std::pair<std::string_view, std::string_view>::first);
    if (file == READY_BLOCK_FILES.end()) {
        return std::unexpected(std::format("The ready block {} is missing from this build", block.Title));
    }
    return ParseCopiedSchematic(file->second);
}

} // namespace GUI

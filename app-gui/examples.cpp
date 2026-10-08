/**
 * @file    examples.cpp
 * @brief   Ready circuits, with their analyses set up, that the user can open to learn from.
 */

#include "examples.h"

#include "example_files.h"
#include <algorithm>
#include <array>
#include <format>

namespace GUI {

namespace {

constexpr auto Examples = std::to_array<Example>({
    {
        .Title = "RC low-pass filter",
        .Description = "A 1k resistor and a 100n capacitor, with the corner at 1.6 kHz. Probe the input and the "
                       "capacitor, then run the AC sweep for the Bode plot, or the transient to see a 1 kHz sine "
                       "attenuated and delayed.",
        .FileName = "rc_low_pass_filter.imcsim",
    },
    {
        .Title = "Full-bridge rectifier",
        .Description = "Four 1N4007 diodes turn a 60 Hz, 17 V peak sine into DC on a 100u reservoir capacitor. "
                       "Probe the load and run the transient to see the ripple.",
        .FileName = "full_bridge_rectifier.imcsim",
    },
    {
        .Title = "LED driver",
        .Description = "A 2N2222A switches a red LED from a 1 kHz, 0 to 5 V pulse. Probe the LED and the base, "
                       "then run the transient.",
        .FileName = "led_driver.imcsim",
    },
    {
        .Title = "BJT output characteristics",
        .Description = "Collector current of a 2N3904 against its collector voltage, one curve per base current "
                       "from 10u to 50u. Probe the collector of Q1 and run the DC sweep.",
        .FileName = "bjt_output_characteristics.imcsim",
    },
});

} // namespace

/**
 * @brief   Lists the examples in menu order.
 * @return  Every example, from the simplest circuit to the most involved.
 */
std::span<const Example> GetExamples() {
    return Examples;
}

/**
 * @brief   Reads an example as if it were opened from a file.
 * @param[in] example  One of GetExamples().
 * @return  The schematic and its settings, or an error when the file is missing from the build.
 */
std::expected<LoadedSchematic, std::string> LoadExample(const Example &example) {
    const auto file =
        std::ranges::find(ExampleFiles, example.FileName, &std::pair<std::string_view, std::string_view>::first);
    if (file == ExampleFiles.end()) {
        return std::unexpected(std::format("The example {} is missing from this build", example.FileName));
    }
    return LoadSchematic(file->second);
}

} // namespace GUI

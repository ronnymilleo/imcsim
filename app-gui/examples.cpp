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
        .Description = "A 1k resistor and a 100n capacitor, with the corner at 1.6 kHz. Run the AC sweep for the "
                       "Bode plot of the input and the capacitor, or the transient to see a 1 kHz sine attenuated "
                       "and delayed.",
        .FileName = "rc_low_pass_filter.imcsim",
    },
    {
        .Title = "Full-bridge rectifier",
        .Description = "Four 1N4007 diodes turn a 60 Hz, 17 V peak sine into DC on a 100u reservoir capacitor. "
                       "Run the transient to see the ripple on the load and the short pulses that recharge it "
                       "through D1.",
        .FileName = "full_bridge_rectifier.imcsim",
    },
    {
        .Title = "LED driver",
        .Description = "A 2N2222A switches a red LED from a 1 kHz, 0 to 5 V pulse. Run the transient to see the "
                       "input and the LED current.",
        .FileName = "led_driver.imcsim",
    },
    {
        .Title = "Op-amp inverting amplifier",
        .Description = "An ideal op-amp amplifies a 0.5 V, 1 kHz sine by -10, set by its 1k input and 10k feedback "
                       "resistors, between 15 V and -15 V supplies. Run the transient to see the inverted output; "
                       "raise the input amplitude past 1.5 V to see it clip, then switch U1 to the uA741 model to see "
                       "a real part clip 1 V short of the supplies.",
        .FileName = "op_amp_inverting_amplifier.imcsim",
    },
    {
        .Title = "Ideal transformer",
        .Description = "A 1:2 transformer built from two controlled sources: E1 sets the secondary at twice the "
                       "primary voltage, and F1 draws twice the load current from the primary. Run the transient: "
                       "the 100 Ohm load gets 10 V from a 5 V source. Then measure only I(F1) and I(R1): the primary "
                       "draws twice the load current, so the source sees 100 / 2^2 = 25 Ohm.",
        .FileName = "ideal_transformer.imcsim",
    },
    {
        .Title = "BJT output characteristics",
        .Description = "Collector current of a 2N3904 against its collector voltage, one curve per base current "
                       "from 10u to 50u. Run the DC sweep.",
        .FileName = "bjt_output_characteristics.imcsim",
    },
    {
        .Title = "Small-signal model of a BJT",
        .Description = "A 2N3904 common-emitter stage on top and its hybrid-pi model below: rpi = 3.17k and a VCCS "
                       "of gm = 52.3m A/V, worked out at its 1.38 mA operating point, with RC and ro = 57k. Run the "
                       "AC sweep: both gains read 47 dB in the middle band, and only the real transistor rolls off "
                       "at high frequency, since the model has no capacitances. The transient shows the same 2.3 V "
                       "out of a 10 mV sine, around 5.5 V on the real collector and around 0 V on the model, which "
                       "only holds the signal.",
        .FileName = "hybrid_pi_model.imcsim",
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

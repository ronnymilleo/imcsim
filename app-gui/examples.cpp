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
        .Title = "Voltage divider with a load",
        .Group = "Basics",
        .Description = "Two 10k resistors split 10 V, but the 10k load on the lower one pulls the output from 5 V "
                       "down to 3.33 V. Run the operating point and compare the voltages and currents of R1, R2 "
                       "and R3.",
        .FileName = "voltage_divider.imcsim",
    },
    {
        .Title = "RC low-pass filter",
        .Group = "Basics",
        .Description = "A 1k resistor and a 100n capacitor, with the corner at 1.6 kHz. Run the AC sweep for the "
                       "Bode plot of the input and the capacitor, or the transient to see a 1 kHz sine attenuated "
                       "and delayed.",
        .FileName = "rc_low_pass_filter.imcsim",
    },
    {
        .Title = "Series RLC resonance",
        .Group = "Basics",
        .Description =
            "A 10 mH inductor, a 100n capacitor and a 100 Ohm resistor in series resonate at 5 kHz, "
            "where the voltage on the resistor peaks with a quality factor of about 3. Run the AC sweep "
            "for the band-pass response and read its -3 dB band, or the transient to see the output build up "
            "at the 5 kHz of the source.",
        .FileName = "series_rlc.imcsim",
    },
    {
        .Title = "Diode curves",
        .Group = "Diodes",
        .Description = "A 1N4148, a 1N4007 and a 1N5819 Schottky, each with its own 100 Ohm resistor, across one "
                       "source swept from 0 to 2 V. Run the DC sweep: each current rises along the same slope once "
                       "its diode conducts, and only the knee voltage differs, lowest for the Schottky and highest "
                       "for the 1N4148.",
        .FileName = "diode_curves.imcsim",
    },
    {
        .Title = "Zener regulator",
        .Group = "Diodes",
        .Description = "A 5.1 V Zener holds the 1k load steady through a 330 Ohm resistor while the supply is swept "
                       "from 0 to 20 V. Run the DC sweep: the output follows the supply up to the breakdown, then "
                       "stays near 5 V while the Zener current grows.",
        .FileName = "zener_regulator.imcsim",
    },
    {
        .Title = "Full-bridge rectifier",
        .Group = "Diodes",
        .Description = "Four 1N4007 diodes turn a 60 Hz, 17 V peak sine into DC on a 100u reservoir capacitor. "
                       "Run the transient to see the ripple on the load and the short pulses that recharge it "
                       "through D1.",
        .FileName = "full_bridge_rectifier.imcsim",
    },
    {
        .Title = "LED driver",
        .Group = "Transistors",
        .Description = "A 2N2222A switches a red LED from a 1 kHz, 0 to 5 V pulse. Run the transient to see the "
                       "input and the LED current.",
        .FileName = "led_driver.imcsim",
    },
    {
        .Title = "NMOS switch",
        .Group = "Transistors",
        .Description = "A 2N7000 switches a 100 Ohm load on 12 V from a 0 to 5 V, 1 kHz pulse at its gate, through "
                       "a 100 Ohm resistor. Run the transient: the drain falls to about 0.6 V and the load draws "
                       "114 mA while the gate is high, and the drain returns to 12 V when it is low. The gate current "
                       "is only a small spike at each edge, the charge of the gate capacitance.",
        .FileName = "nmos_switch.imcsim",
    },
    {
        .Title = "MOSFET output characteristics",
        .Group = "Transistors",
        .Description = "Drain current of a 2N7000 against its drain voltage, one curve per gate voltage from 2.5 V "
                       "to 3.5 V. Run the DC sweep: each curve rises in the ohmic region and flattens in saturation, "
                       "and a higher gate voltage lifts the plateau.",
        .FileName = "mosfet_output_characteristics.imcsim",
    },
    {
        .Title = "BJT output characteristics",
        .Group = "Transistors",
        .Description = "Collector current of a 2N3904 against its collector voltage, one curve per base current "
                       "from 10u to 50u. Run the DC sweep.",
        .FileName = "bjt_output_characteristics.imcsim",
    },
    {
        .Title = "Current mirror",
        .Group = "Transistors",
        .Description = "Two 2N3904 copy the current of the reference branch, a 10k resistor on a supply stepped from "
                       "3 V to 11 V, into the output branch. Run the DC sweep of the output supply: each curve is "
                       "nearly flat at the reference current once Q2 leaves saturation, above about 0.2 V, and rises "
                       "slightly with the voltage because of the Early effect.",
        .FileName = "current_mirror.imcsim",
    },
    {
        .Title = "Common-emitter amplifier",
        .Group = "Transistors",
        .Description = "A 2N3904 biased by a 51k and 10k divider at 1.2 mA, with a 4.7k collector resistor, a 1k "
                       "emitter resistor bypassed by 100u, 10u coupling capacitors and a 10k load. Run the AC sweep: "
                       "the mid-band gain is 43 dB, with corners at 77 Hz, from the coupling and bypass capacitors, "
                       "and 18 MHz, from the transistor capacitances. The transient turns a 10 mV, 1 kHz sine into "
                       "an inverted 1.4 V one around 6 V of collector bias.",
        .FileName = "common_emitter_amplifier.imcsim",
    },
    {
        .Title = "Small-signal model of a BJT",
        .Group = "Transistors",
        .Description = "A 2N3904 common-emitter stage on top and its hybrid-pi model below: rpi = 3.17k and a VCCS "
                       "of gm = 52.3m A/V, worked out at its 1.38 mA operating point, with RC and ro = 57k. Run the "
                       "AC sweep: both gains read 47 dB in the middle band, and only the real transistor rolls off "
                       "at high frequency, since the model has no capacitances. The transient shows the same 2.3 V "
                       "out of a 10 mV sine, around 5.5 V on the real collector and around 0 V on the model, which "
                       "only holds the signal.",
        .FileName = "hybrid_pi_model.imcsim",
    },
    {
        .Title = "Class AB amplifier",
        .Group = "Transistors",
        .Description = "Two push-pull emitter followers, a 2N3904 and a 2N3906, drive a 16 Ohm load from a 1.5 V, "
                       "1 kHz sine and 12 V and -12 V supplies. On the left the bases are tied together, as in a "
                       "class B stage: neither transistor conducts below about 0.6 V, so the output stays flat around "
                       "zero (crossover distortion). On the right two 1N4148 diodes bias both transistors just on, at "
                       "a few mA, and the output follows the input. Run the transient and compare both outputs with "
                       "the input: the net labels IN, OUT_B and OUT_AB name them in the plots, and the two IN labels "
                       "join the input to the class AB stage with no wire between them.",
        .FileName = "class_ab_amplifier.imcsim",
    },
    {
        .Title = "Op-amp inverting amplifier",
        .Group = "Op-amps",
        .Description = "An ideal op-amp amplifies a 0.5 V, 1 kHz sine by -10, set by its 1k input and 10k feedback "
                       "resistors, between 15 V and -15 V supplies. Run the transient to see the inverted output; "
                       "raise the input amplitude past 1.5 V to see it clip, then switch U1 to the uA741 model to see "
                       "a real part clip 1 V short of the supplies.",
        .FileName = "op_amp_inverting_amplifier.imcsim",
    },
    {
        .Title = "Op-amp non-inverting amplifier",
        .Group = "Op-amps",
        .Description = "An ideal op-amp with a 1 MHz gain-bandwidth product amplifies a 0.5 V, 1 kHz sine by "
                       "1 + 10k / 1k = 11, or 20.8 dB, between 15 V and -15 V supplies. Run the AC sweep: the gain "
                       "is flat and then falls at 91 kHz, the gain-bandwidth product divided by the gain. The "
                       "transient shows the output in phase with the input, unlike the inverting amplifier.",
        .FileName = "non_inverting_amplifier.imcsim",
    },
    {
        .Title = "Op-amp integrator",
        .Group = "Op-amps",
        .Description = "An inverting integrator with a 10k input resistor and a 10n feedback capacitor, plus a 1M "
                       "resistor across the capacitor that keeps the DC gain finite. Run the AC sweep: above the "
                       "16 Hz corner the gain falls 20 dB per decade and crosses 0 dB near 1.6 kHz, which is 1 / "
                       "(2 pi R C). The transient shows a 1 V, 1 kHz sine come out as a cosine of 1.6 V that starts "
                       "at 0 V, so it sits below zero at first and the offset fades slowly through the 1M resistor.",
        .FileName = "op_amp_integrator.imcsim",
    },
    {
        .Title = "Schmitt trigger",
        .Group = "Op-amps",
        .Description = "An op-amp with positive feedback, 1k to ground and 10k from the output, flips its output "
                       "between about 15 V and -15 V only when the input passes +-1.36 V, the output swing over 11, "
                       "and not at 0 V. Run the transient: a 3 V, 1 kHz sine at the inverting input gives a square "
                       "wave, and the voltage on the other input steps between the two thresholds.",
        .FileName = "schmitt_trigger.imcsim",
    },
    {
        .Title = "Sallen-Key low-pass filter",
        .Group = "Op-amps",
        .Description = "A second-order low-pass filter around a unity-gain op-amp: two 10k resistors, 22n and 10n "
                       "capacitors, with a corner near 1.1 kHz and a quality factor of 0.74, close to a Butterworth "
                       "response. Run the AC sweep: the gain is flat up to 1 kHz, is 3 dB down at 1.12 kHz and falls "
                       "40 dB per decade beyond it.",
        .FileName = "sallen_key_low_pass.imcsim",
    },
    {
        .Title = "Ideal transformer",
        .Group = "Controlled sources",
        .Description = "A 1:2 transformer built from two controlled sources: E1 sets the secondary at twice the "
                       "primary voltage, and F1 draws twice the load current from the primary. Run the transient: "
                       "the 100 Ohm load gets 10 V from a 5 V source. Then measure only I(F1) and I(R1): the primary "
                       "draws twice the load current, so the source sees 100 / 2^2 = 25 Ohm.",
        .FileName = "ideal_transformer.imcsim",
    },
});

} // namespace

/**
 * @brief   Lists the examples in menu order.
 * @return  Every example, grouped by topic, each group from the simplest circuit to the most involved.
 */
std::span<const Example> GetExamples() {
    return Examples;
}

/**
 * @brief   Lists the submenus the examples are sorted into.
 * @return  The name of each group once, in the order of its first example.
 */
std::vector<const char *> GetExampleGroups() {
    std::vector<const char *> groups;
    for (const Example &example : Examples) {
        if (std::ranges::none_of(groups,
                                 [&example](const char *group) { return std::string_view(group) == example.Group; })) {
            groups.push_back(example.Group);
        }
    }
    return groups;
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

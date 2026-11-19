/**
 * @file    ui_source_tests.cpp
 * @brief   Tests for how the symbol of an independent source labels its waveform.
 */

#include "ui_elements/ui_source.h"

#include "circuit/components/current_source.h"
#include "circuit/components/voltage_source.h"
#include <catch2/catch_test_macros.hpp>
#include <optional>

TEST_CASE("A DC source is labeled by its value, and the other waveforms by their main figures", "[ui_source]") {
    Core::VoltageSource source;
    CHECK_FALSE(GUI::DescribeWaveform(source).has_value());

    source.SetSourceType(Core::Source::SourceType::AC);
    CHECK(GUI::DescribeWaveform(source) == "1V 1kHz");

    source.SetSourceType(Core::Source::SourceType::PULSE);
    CHECK(GUI::DescribeWaveform(source) == "0V/5V 1kHz");
}

TEST_CASE("The waveform of a current source reads in amperes", "[ui_source]") {
    Core::CurrentSource source;
    source.SetSourceType(Core::Source::SourceType::AC);
    source.SetAC({.Amplitude = 2e-3, .Frequency = 50.0, .Offset = 0.0, .Magnitude = 1.0});
    CHECK(GUI::DescribeWaveform(source) == "2mA 50Hz");
}

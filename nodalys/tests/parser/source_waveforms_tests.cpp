/**
 * @file    source_waveforms_tests.cpp
 * @brief   Tests for recognizing the waveform a word of an independent source line starts.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "parser/sources/source_waveforms.h"

#include <string>
#include <utility>

TEST_CASE("A waveform word is known by its name and its opening parenthesis, in any case", "[source_waveforms]") {
    using WordAndWaveform = std::pair<std::string, Nodalys::SourceWaveform>;
    const auto [word, waveform] = GENERATE(WordAndWaveform{"PULSE(0 1)", Nodalys::SourceWaveform::PULSE},
                                           WordAndWaveform{"sin(0 1 1k)", Nodalys::SourceWaveform::SINE},
                                           WordAndWaveform{"Sine(0 1 1k)", Nodalys::SourceWaveform::SINE},
                                           WordAndWaveform{"EXP(0 1)", Nodalys::SourceWaveform::EXP},
                                           WordAndWaveform{"pwl(0 0 1 1)", Nodalys::SourceWaveform::PWL},
                                           WordAndWaveform{"SFFM(0 1 1k)", Nodalys::SourceWaveform::SFFM},
                                           WordAndWaveform{"am(1 0 1k 10k)", Nodalys::SourceWaveform::AM},
                                           WordAndWaveform{"TRNOISE(1m 1n)", Nodalys::SourceWaveform::TRNOISE},
                                           WordAndWaveform{"trrandom(1 1n)", Nodalys::SourceWaveform::TRRANDOM});
    CAPTURE(word);
    CHECK(Nodalys::FindSourceWaveform(word) == waveform);
}

TEST_CASE("A word that only resembles a waveform starts none", "[source_waveforms]") {
    // A waveform needs its parenthesis right after its name, and another word that starts the same is no waveform
    for (const std::string word : {"PULSE", "SIN (0 1 1k)", "SINUS(0 1)", "DC", "5", "AMP(1)", ""}) {
        CAPTURE(word);
        CHECK(Nodalys::FindSourceWaveform(word) == Nodalys::SourceWaveform::NONE);
    }
}

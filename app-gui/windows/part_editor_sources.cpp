/**
 * @file    part_editor_sources.cpp
 * @brief   The properties of voltage and current sources: the waveform they follow and its parameters.
 */

#include "windows/part_editor_sources.h"

#include "base/theme.h"
#include "misc/cpp/imgui_stdlib.h"
#include "spice/spice_value.h"
#include "windows/part_fields.h"
#include <cmath>
#include <format>
#include <optional>
#include <vector>

namespace GUI {

namespace {

constexpr auto AC_FIELDS = std::to_array<ParameterField<Core::ACParameters>>({
    {"Amplitude", &Core::ACParameters::Amplitude, nullptr},
    {"Frequency", &Core::ACParameters::Frequency, "Hz"},
    {"Offset", &Core::ACParameters::Offset, nullptr},
    {"AC magnitude", &Core::ACParameters::Magnitude, nullptr},
});

constexpr auto PULSE_FIELDS = std::to_array<ParameterField<Core::PulseParameters>>({
    {"Low", &Core::PulseParameters::Low, nullptr},
    {"High", &Core::PulseParameters::High, nullptr},
    {"Delay", &Core::PulseParameters::Delay, "s"},
    {"Rise", &Core::PulseParameters::RiseTime, "s"},
    {"Fall", &Core::PulseParameters::FallTime, "s"},
    {"Width", &Core::PulseParameters::Width, "s"},
    {"Period", &Core::PulseParameters::Period, "s"},
});

constexpr auto EXP_FIELDS = std::to_array<ParameterField<Core::EXPParameters>>({
    {"Start", &Core::EXPParameters::InitialValue, nullptr},
    {"Peak", &Core::EXPParameters::PeakValue, nullptr},
    {"Rise delay", &Core::EXPParameters::RiseDelay, "s"},
    {"Rise tau", &Core::EXPParameters::RiseTimeConstant, "s"},
    {"Fall delay", &Core::EXPParameters::FallDelay, "s"},
    {"Fall tau", &Core::EXPParameters::FallTimeConstant, "s"},
});

constexpr auto NOISE_FIELDS = std::to_array<ParameterField<Core::NoiseParameters>>({
    {"Offset", &Core::NoiseParameters::Offset, nullptr},
    {"White rms", &Core::NoiseParameters::WhiteAmplitude, nullptr},
    {"Sample step", &Core::NoiseParameters::TimeStep, "s"},
    {"1/f exponent", &Core::NoiseParameters::FlickerExponent, ""},
    {"1/f amplitude", &Core::NoiseParameters::FlickerAmplitude, nullptr},
    {"Telegraph", &Core::NoiseParameters::TelegraphAmplitude, nullptr},
    {"Capture time", &Core::NoiseParameters::CaptureTime, "s"},
    {"Emission time", &Core::NoiseParameters::EmissionTime, "s"},
});

/// What the first and second parameters of each distribution are called; Poisson lambda is a count, with no unit.
struct RandomLabels {
    const char *First;
    const char *Second;
    const char *FirstUnit;
};

constexpr RandomLabels GetRandomLabels(const Core::RandomDistribution distribution) {
    switch (distribution) {
    case Core::RandomDistribution::UNIFORM:
        return {"Range +/-", "Offset", nullptr};
    case Core::RandomDistribution::EXPONENTIAL:
        return {"Mean", "Offset", nullptr};
    case Core::RandomDistribution::POISSON:
        return {"Lambda", "Offset", ""};
    case Core::RandomDistribution::GAUSSIAN:
    default:
        return {"Std deviation", "Mean", nullptr};
    }
}

constexpr auto SFFM_FIELDS = std::to_array<ParameterField<Core::SFFMParameters>>({
    {"Offset", &Core::SFFMParameters::Offset, nullptr},
    {"Amplitude", &Core::SFFMParameters::Amplitude, nullptr},
    {"Carrier", &Core::SFFMParameters::CarrierFrequency, "Hz"},
    {"Mod index", &Core::SFFMParameters::ModulationIndex, ""},
    {"Signal", &Core::SFFMParameters::SignalFrequency, "Hz"},
});

constexpr auto AM_FIELDS = std::to_array<ParameterField<Core::AMParameters>>({
    {"Amplitude", &Core::AMParameters::Amplitude, nullptr},
    {"Offset", &Core::AMParameters::Offset, nullptr},
    {"Mod freq", &Core::AMParameters::ModulatingFrequency, "Hz"},
    {"Carrier", &Core::AMParameters::CarrierFrequency, "Hz"},
    {"Delay", &Core::AMParameters::Delay, "s"},
});

/**
 * @brief   Draws the choice of waveform of a source, one radio button per kind, in rows as wide as the window.
 * @param[in,out] source  Source whose waveform is chosen.
 */
void DrawSourceTypes(Core::Source &source, Schematic &schematic) {
    using SourceType = Core::Source::SourceType;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool is_first = true;
    for (const SourceType type : {SourceType::DC, SourceType::AC, SourceType::PULSE, SourceType::EXP, SourceType::SFFM,
                                  SourceType::AM, SourceType::PWL, SourceType::NOISE, SourceType::RANDOM}) {
        const float button_width =
            ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(Core::GetSourceTypeName(type)).x;
        // A button that would cross the edge starts the next row, so a narrow Properties column cuts none of them
        if (!is_first && ImGui::GetItemRectMax().x + style.ItemSpacing.x + button_width <= right_edge) {
            ImGui::SameLine();
        }
        is_first = false;
        if (ImGui::RadioButton(Core::GetSourceTypeName(type), source.GetSourceType() == type) &&
            source.GetSourceType() != type) {
            source.SetSourceType(type);
            schematic.MarkModified();
        }
    }
}

void DrawSine(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::ACParameters ac = source.GetAC();
    const auto is_valid = [&source](const Core::ACParameters &candidate) { return source.IsValidAC(candidate); };
    if (DrawParameterFields(AC_FIELDS, fields.AC, source.GetUnit(), is_valid, ac)) {
        source.SetAC(ac);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("The amplitude is the peak of the sine in a transient");
    ImGui::TextDisabled("The AC magnitude drives the AC sweep; 1 reads as gain");
}

void DrawPulse(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::PulseParameters pulse = source.GetPulse();
    const auto is_valid = [&source](const Core::PulseParameters &candidate) { return source.IsValidPulse(candidate); };
    if (DrawParameterFields(PULSE_FIELDS, fields.Pulse, source.GetUnit(), is_valid, pulse)) {
        source.SetPulse(pulse);
        schematic.MarkModified();
    }

    ImGui::TextDisabled("%s", std::format("Repeats at {}Hz", Core::FormatValue(1.0 / pulse.Period)).c_str());
    if (pulse.RiseTime + pulse.Width + pulse.FallTime > pulse.Period) {
        ImGui::TextColored(GetWarningTextColor(), "Rise, width and fall add up to more than the period");
    }
}

void DrawEXP(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::EXPParameters exp = source.GetEXP();
    const auto is_valid = [&source](const Core::EXPParameters &candidate) { return source.IsValidEXP(candidate); };
    if (DrawParameterFields(EXP_FIELDS, fields.EXP, source.GetUnit(), is_valid, exp)) {
        source.SetEXP(exp);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Rises towards the peak, then decays back towards the start");
}

void DrawSFFM(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::SFFMParameters sffm = source.GetSFFM();
    const auto is_valid = [&source](const Core::SFFMParameters &candidate) { return source.IsValidSFFM(candidate); };
    if (DrawParameterFields(SFFM_FIELDS, fields.SFFM, source.GetUnit(), is_valid, sffm)) {
        source.SetSFFM(sffm);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("A carrier whose phase swings with the signal");
}

void DrawAM(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::AMParameters am = source.GetAM();
    const auto is_valid = [&source](const Core::AMParameters &candidate) { return source.IsValidAM(candidate); };
    if (DrawParameterFields(AM_FIELDS, fields.AM, source.GetUnit(), is_valid, am)) {
        source.SetAM(am);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("The modulating and carrier sines multiply; the offset shifts the whole waveform");
}

/**
 * @brief   The points apply as soon as the text reads, so a half-typed field keeps the last valid shape.
 */
void DrawPWL(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    DrawFieldLabel("Points");
    const bool changed = ImGui::InputText("##pwl", &fields.PWLText);
    const std::optional<std::vector<Core::PWLPoint>> points = Core::ParsePWLPoints(fields.PWLText);
    if (changed && points) {
        source.SetPWL({.Points = *points});
        schematic.MarkModified();
    }
    if (!points) {
        ImGui::TextColored(GetErrorTextColor(), "At least two \"time value\" pairs, times increasing from zero");
    } else {
        ImGui::TextDisabled("For example: 0 0, 1m 5, 2m 0");
    }
}

/**
 * @brief   The density below the fields is what the Spectrum tab of a run shows as a floor: the white noise is flat up
 * to half the sampling rate.
 */
void DrawNoise(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::NoiseParameters noise = source.GetNoise();
    const auto is_valid = [&source](const Core::NoiseParameters &candidate) { return source.IsValidNoise(candidate); };
    if (DrawParameterFields(NOISE_FIELDS, fields.Noise, source.GetUnit(), is_valid, noise)) {
        source.SetNoise(noise);
        schematic.MarkModified();
    }
    if (noise.WhiteAmplitude > 0.0 && noise.TimeStep > 0.0) {
        const double density = noise.WhiteAmplitude * std::sqrt(2.0 * noise.TimeStep);
        ImGui::TextDisabled("%s", std::format("White noise: {}{}/rtHz, flat up to {}Hz", Core::FormatValue(density),
                                              source.GetUnit(), Core::FormatValue(0.5 / noise.TimeStep))
                                      .c_str());
    }
    ImGui::TextDisabled("1/f amplitude, telegraph amplitude and white rms at 0 turn that part off");
    ImGui::TextDisabled("Noise changes on each run; for noise over a signal, put a second source in series");
}

/**
 * @brief   The labels of the two parameters follow the distribution, as the ngspice TRRANDOM source defines them.
 */
void DrawRandom(Core::Source &source, SourceFields &fields, Schematic &schematic) {
    Core::RandomParameters random = source.GetRandom();
    DrawFieldLabel("Distribution");
    if (ImGui::BeginCombo("##distribution", Core::GetRandomDistributionName(random.Distribution))) {
        for (const Core::RandomDistribution distribution :
             {Core::RandomDistribution::UNIFORM, Core::RandomDistribution::GAUSSIAN,
              Core::RandomDistribution::EXPONENTIAL, Core::RandomDistribution::POISSON}) {
            const bool is_selected = distribution == random.Distribution;
            if (ImGui::Selectable(Core::GetRandomDistributionName(distribution), is_selected) && !is_selected) {
                random.Distribution = distribution;
                if (source.IsValidRandom(random)) {
                    source.SetRandom(random);
                    schematic.MarkModified();
                }
            }
        }
        ImGui::EndCombo();
    }
    random = source.GetRandom();
    const RandomLabels labels = GetRandomLabels(random.Distribution);
    const char *unit = source.GetUnit();
    const auto is_valid = [&source, &random](double Core::RandomParameters::*member, const double value) {
        Core::RandomParameters candidate = random;
        candidate.*member = value;
        return source.IsValidRandom(candidate);
    };
    bool changed = false;
    const auto draw = [&](ValueField &field, const char *label, const char *field_unit,
                          double Core::RandomParameters::*member) {
        if (const std::optional<double> value =
                field.Draw(label, field_unit, [&](const double candidate) { return is_valid(member, candidate); })) {
            random.*member = *value;
            changed = true;
        }
    };
    draw(fields.Random[0], "Sample step", "s", &Core::RandomParameters::TimeStep);
    draw(fields.Random[1], "Delay", "s", &Core::RandomParameters::Delay);
    draw(fields.Random[2], labels.First, labels.FirstUnit != nullptr ? labels.FirstUnit : unit,
         &Core::RandomParameters::First);
    draw(fields.Random[3], labels.Second, unit, &Core::RandomParameters::Second);
    if (changed) {
        source.SetRandom(random);
        schematic.MarkModified();
    }
    ImGui::TextDisabled("Each value is held for the sample step; the seed is in the Simulation settings");
}

} // namespace

/**
 * @brief   Loads the texts of the fields of every waveform from the parameters the source holds.
 */
void LoadSourceFields(SourceFields &fields, const Core::Source &source) {
    LoadParameterFields(AC_FIELDS, fields.AC, source.GetAC());
    LoadParameterFields(PULSE_FIELDS, fields.Pulse, source.GetPulse());
    LoadParameterFields(EXP_FIELDS, fields.EXP, source.GetEXP());
    LoadParameterFields(SFFM_FIELDS, fields.SFFM, source.GetSFFM());
    LoadParameterFields(AM_FIELDS, fields.AM, source.GetAM());
    LoadParameterFields(NOISE_FIELDS, fields.Noise, source.GetNoise());
    fields.Random[0].Load(source.GetRandom().TimeStep);
    fields.Random[1].Load(source.GetRandom().Delay);
    fields.Random[2].Load(source.GetRandom().First);
    fields.Random[3].Load(source.GetRandom().Second);
    fields.PWLText = Core::FormatPWLPoints(source.GetPWL().Points);
}

void DrawSource(Core::Source &source, SourceFields &fields, ValueField &main_value, Schematic &schematic) {
    using SourceType = Core::Source::SourceType;
    DrawSourceTypes(source, schematic);
    switch (source.GetSourceType()) {
    case SourceType::DC: {
        const char *label = source.GetType() == Core::ComponentType::CURRENT_SOURCE ? "Current" : "Voltage";
        if (const std::optional<double> value = main_value.Draw(label, source.GetUnit(), AnyValue)) {
            source.SetValue(*value);
            schematic.MarkModified();
        }
        break;
    }
    case SourceType::AC:
        DrawSine(source, fields, schematic);
        break;
    case SourceType::PULSE:
        DrawPulse(source, fields, schematic);
        break;
    case SourceType::EXP:
        DrawEXP(source, fields, schematic);
        break;
    case SourceType::SFFM:
        DrawSFFM(source, fields, schematic);
        break;
    case SourceType::AM:
        DrawAM(source, fields, schematic);
        break;
    case SourceType::PWL:
        DrawPWL(source, fields, schematic);
        break;
    case SourceType::NOISE:
        DrawNoise(source, fields, schematic);
        break;
    case SourceType::RANDOM:
        DrawRandom(source, fields, schematic);
        break;
    default:
        break;
    }
}

} // namespace GUI

/**
 * @file    source.h
 * @brief   Independent sources with a DC, sine, pulse, exponential, single-frequency FM, amplitude-modulated,
 *          piecewise linear, noise or random waveform, shared by voltage and current sources.
 */

#ifndef IMCSIM_SOURCE_H
#define IMCSIM_SOURCE_H

#include "components/component.h"
#include <nodalys/types.h>

namespace Core {

/**
 * @struct  ACParameters
 * @brief   An AC source: the sine it makes in a transient, as in the SPICE SIN source, and its magnitude in an AC
 *          sweep, as the SPICE AC value; amplitude, offset and magnitude in the unit of the source, frequency in
 *          hertz.
 * @details The amplitude is the peak of the sine. The magnitude is apart from it, so a transient can use a small,
 *          realistic signal while the AC sweep, which is linear, uses 1 and reads directly as gain.
 */
using ACParameters = nodalys::SineParameters;

/**
 * @struct  PulseParameters
 * @brief   Shape of a repeating pulse, as in the SPICE PULSE source: times in seconds, levels in the unit of
 *          the source.
 * @details The source sits at Low for Delay, ramps to High in RiseTime, stays there for Width, ramps back in
 *          FallTime and starts again every Period.
 */
using PulseParameters = nodalys::PulseParameters;

/**
 * @struct  EXPParameters
 * @brief   Shape of an exponential source, as in the SPICE EXP source: values in the unit of the source, times in
 *          seconds.
 * @details The source starts at InitialValue, rises towards PeakValue with time constant RiseTimeConstant from
 *          RiseDelay on, then decays back towards InitialValue with time constant FallTimeConstant from FallDelay on.
 */
using EXPParameters = nodalys::ExpParameters;

/**
 * @struct  SFFMParameters
 * @brief   Shape of a single-frequency FM source, as in the SPICE SFFM source: levels in the unit of the source
 *          and frequencies in hertz.
 * @details The source is Offset plus Amplitude times a carrier at CarrierFrequency whose phase swings by
 *          ModulationIndex times a sine at SignalFrequency.
 */
using SFFMParameters = nodalys::SffmParameters;

/**
 * @struct  AMParameters
 * @brief   Shape of an amplitude-modulated source, as in the SPICE AM source: levels in the unit of the source and
 *          frequencies in hertz.
 * @details The source is Offset plus Amplitude times a sine at ModulatingFrequency times a sine at
 *          CarrierFrequency, both measured from Delay on; until the delay the transient holds it at 0, while the
 *          operating point stays at the offset.
 */
using AMParameters = nodalys::AmParameters;

/**
 * @struct  PWLPoint
 * @brief   One point of a piecewise linear source: a time in seconds and the value the source holds there, in the
 *          unit of the source.
 */
using PWLPoint = nodalys::PwlPoint;

/**
 * @struct  PWLParameters
 * @brief   Shape of a piecewise linear source, as in the SPICE PWL source: the value at each time, joined by straight
 *          lines and held at the first and last value outside the points.
 * @details The points are ordered by time; at least two are needed, times are not negative and strictly increase.
 */
using PWLParameters = nodalys::PwlParameters;

/**
 * @struct  NoiseParameters
 * @brief   A source of transient noise, as in the SPICE TRNOISE source: levels in the unit of the source, times in
 *          seconds.
 * @details The source is Offset plus up to three kinds of noise: Gaussian white noise of WhiteAmplitude rms with a new
 *          sample every TimeStep (the output is a straight line between samples), 1/f noise of FlickerAmplitude with
 *          the power falling as FlickerExponent, and a random telegraph signal that jumps between 0 and
 *          TelegraphAmplitude, staying CaptureTime up and EmissionTime down on average. A kind whose amplitude is 0 is
 *          off. The white noise has a flat density of WhiteAmplitude times the square root of 2 TimeStep per root hertz
 *          up to half the sampling rate, 1 / (2 TimeStep).
 */
struct NoiseParameters {
    double Offset = 0.0;
    double WhiteAmplitude = 0.1;
    double TimeStep = 10e-6;
    double FlickerExponent = 1.0;
    double FlickerAmplitude = 0.0;
    double TelegraphAmplitude = 0.0;
    double CaptureTime = 1e-3;
    double EmissionTime = 1e-3;
};

/**
 * @enum    RandomDistribution
 * @brief   Distribution of the values of a random source, numbered as the type of the SPICE TRRANDOM source.
 */
enum class RandomDistribution {
    UNIFORM = 1,
    GAUSSIAN = 2,
    EXPONENTIAL = 3,
    POISSON = 4
};

/**
 * @struct  RandomParameters
 * @brief   A source of random values held for TimeStep each, as in the SPICE TRRANDOM source: levels in the unit of
 *          the source, times in seconds.
 * @details First and Second mean different things for each distribution: the range around the offset and the offset
 *          for a uniform one, the standard deviation and the mean for a Gaussian, the mean and the offset for an
 *          exponential, lambda and the offset for a Poisson one. Before Delay the source holds Second.
 */
struct RandomParameters {
    RandomDistribution Distribution = RandomDistribution::GAUSSIAN;
    double TimeStep = 10e-6;
    double Delay = 0.0;
    double First = 0.1;
    double Second = 0.0;
};

/**
 * @class   Source
 * @brief   An independent source between two nodes, whose value follows one of nine waveforms.
 * @details A DC source holds its level in the component value. The other types keep it but do not use it, holding
 *          their own parameters instead: a sine (AC), a repeating pulse, an exponential, a single-frequency FM, an
 *          amplitude modulation, piecewise linear points, noise or random values. Every type keeps its parameters
 *          while another one is selected. Levels are in the unit of the derived source, volts or amperes.
 */
class Source : public Component {
public:
    /**
     * @enum    SourceType
     * @brief   Waveform of the source: direct current, a sine, a repeating pulse, an exponential, a
     *          single-frequency FM, an amplitude modulation, piecewise linear points, noise or random values.
     */
    enum class SourceType {
        DC,
        AC,
        PULSE,
        EXP,
        SFFM,
        AM,
        PWL,
        NOISE,
        RANDOM
    };

    ~Source() override = default;

    // Waveform
    SourceType GetSourceType() const;
    void SetSourceType(SourceType type);

    // Sine parameters, used when the source is AC
    const ACParameters &GetAC() const;
    void SetAC(const ACParameters &ac);
    bool IsValidAC(const ACParameters &ac) const;

    // Pulse parameters, used when the source is a pulse
    const PulseParameters &GetPulse() const;
    void SetPulse(const PulseParameters &pulse);
    bool IsValidPulse(const PulseParameters &pulse) const;

    // Exponential parameters, used when the source is an exponential
    const EXPParameters &GetEXP() const;
    void SetEXP(const EXPParameters &exp);
    bool IsValidEXP(const EXPParameters &exp) const;

    // Single-frequency FM parameters, used when the source is an SFFM
    const SFFMParameters &GetSFFM() const;
    void SetSFFM(const SFFMParameters &sffm);
    bool IsValidSFFM(const SFFMParameters &sffm) const;

    // Amplitude modulation parameters, used when the source is an AM
    const AMParameters &GetAM() const;
    void SetAM(const AMParameters &am);
    bool IsValidAM(const AMParameters &am) const;

    // Piecewise linear parameters, used when the source is a PWL
    const PWLParameters &GetPWL() const;
    void SetPWL(const PWLParameters &pwl);
    bool IsValidPWL(const PWLParameters &pwl) const;

    // Noise parameters, used when the source is a noise
    const NoiseParameters &GetNoise() const;
    void SetNoise(const NoiseParameters &noise);
    bool IsValidNoise(const NoiseParameters &noise) const;

    // Random parameters, used when the source is a random one
    const RandomParameters &GetRandom() const;
    void SetRandom(const RandomParameters &random);
    bool IsValidRandom(const RandomParameters &random) const;

    // Time resolution
    std::optional<double> GetSignalStep() const;

protected:
    Source(ComponentType type, double value, double amplitude, double pulse_high);

private:
    SourceType m_Type{SourceType::DC};
    ACParameters m_AC;
    PulseParameters m_Pulse;
    EXPParameters m_EXP;
    SFFMParameters m_SFFM;
    AMParameters m_AM;
    PWLParameters m_PWL;
    NoiseParameters m_Noise;
    RandomParameters m_Random;
};

// Sources
bool IsSource(ComponentType type);
const char *GetSourceTypeName(Source::SourceType type);
std::optional<Source::SourceType> ParseSourceType(std::string_view text);
const char *GetRandomDistributionName(RandomDistribution distribution);
std::optional<RandomDistribution> ParseRandomDistribution(std::string_view text);

// Piecewise linear points, edited as one text of "time value" pairs such as "0 0, 1m 5, 2m 0"
std::optional<std::vector<PWLPoint>> ParsePWLPoints(std::string_view text);
std::string FormatPWLPoints(const std::vector<PWLPoint> &points);

} // namespace Core

#endif // IMCSIM_SOURCE_H

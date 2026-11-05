/**
 * @file    monte_carlo_results.h
 * @brief   What a Monte Carlo analysis keeps of every run: the values drawn, what the run measured, and the statistics
 *          and histogram over the runs.
 * @details Pure functions with no ImGui, so the tests run them. The traces of a run are kept for only some runs, as a
 *          StepFamily; this table is small enough to keep for all of them.
 */

#ifndef IMCSIM_MONTE_CARLO_RESULTS_H
#define IMCSIM_MONTE_CARLO_RESULTS_H

#include "simulation/simulation_results.h"
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace GUI {

/**
 * @enum    MeasureKind
 * @brief   What is measured on the voltage of a node in each run, after the analysis: the readings of the oscilloscope
 *          for a transient, of the Bode plot for an AC sweep and of the curve tracer for a DC sweep.
 */
enum class MeasureKind {
    MEAN,
    RMS,
    PEAK_TO_PEAK,
    FREQUENCY,
    PEAK_GAIN,
    LOWER_CUTOFF,
    UPPER_CUTOFF,
    UNITY_GAIN_FREQUENCY,
    PHASE_MARGIN,
    MINIMUM,
    MAXIMUM,
};

/**
 * @struct  MeasureColumn
 * @brief   One quantity measured on one node.
 */
struct MeasureColumn {
    int Node = 0;
    MeasureKind Kind = MeasureKind::MEAN;

    bool operator==(const MeasureColumn &) const = default;
};

/**
 * @struct  Measurement
 * @brief   What one result measured: the columns and a value for each, or none where the trace has no such reading,
 *          such as a cutoff the response never reaches.
 */
struct Measurement {
    std::vector<MeasureColumn> Columns;
    std::vector<std::optional<double>> Values;
};

/**
 * @struct  MonteCarloRun
 * @brief   One run: the value each varied part took and what the run measured.
 */
struct MonteCarloRun {
    /// One per varied part, as MonteCarloTable::Parts lists them.
    std::vector<double> Values;
    /// One per column of the table; empty for a run that failed.
    std::vector<std::optional<double>> Measures;
    bool IsFailed = false;
    /// Why the run failed.
    std::string Error;
};

/**
 * @struct  MonteCarloTable
 * @brief   Every run of a Monte Carlo analysis, the nominal one first.
 * @details Runs[0] is the nominal run, which keeps every value; Runs[k] is run k, drawn from the seed of the analysis.
 *          A run that was stopped before it started is missing, so the table can hold fewer runs than were asked for.
 */
struct MonteCarloTable {
    /// Names of the varied parts, as the schematic shows them.
    std::vector<std::string> Parts;
    /// Unit of the value of each part, such as "Ohm".
    std::vector<std::string> Units;
    std::vector<MeasureColumn> Columns;
    std::vector<MonteCarloRun> Runs;
    /// Runs asked for, without the nominal one.
    std::size_t RequestedRuns = 0;
};

/**
 * @struct  SampleStatistics
 * @brief   What the runs of one measured quantity add up to.
 */
struct SampleStatistics {
    std::size_t Count = 0;
    double Minimum = 0.0;
    double Maximum = 0.0;
    double Mean = 0.0;
    /// Sample standard deviation, with n - 1 in the divisor; zero for a single sample.
    double StandardDeviation = 0.0;
    double Percentile1 = 0.0;
    double Percentile99 = 0.0;
};

/**
 * @struct  Histogram
 * @brief   How many samples fall in each of equal bins from the smallest to the largest.
 */
struct Histogram {
    double From = 0.0;
    double To = 0.0;
    std::vector<std::size_t> Counts;
};

/**
 * @struct  RunSort
 * @brief   How the table of runs is ordered: by run number, by the value of a part or by a measured column.
 */
struct RunSort {
    enum class Key {
        RUN,
        PART,
        MEASURE,
    };

    Key By = Key::RUN;
    /// Part or column to sort by.
    std::size_t Index = 0;
    bool IsAscending = true;
};

// Measuring
Measurement MeasureTransient(const Core::Transient &transient);
Measurement MeasureACSweep(const Core::ACSweep &sweep);
Measurement MeasureDCSweep(const Core::DCSweep &sweep);
const char *GetMeasureName(MeasureKind kind);
const char *GetMeasureUnit(MeasureKind kind);

// Columns
std::vector<int> ListTableNodes(const MonteCarloTable &table);
std::vector<std::size_t> ListNodeColumns(const MonteCarloTable &table, int node);
std::size_t FindDefaultColumn(const MonteCarloTable &table, std::span<const int> preferred_nodes);

// Statistics
std::vector<double> CollectMeasure(const MonteCarloTable &table, std::size_t column);
double ComputeRelativeSpread(std::span<const double> samples, double absolute_floor);
double GetMeasureFloor(MeasureKind kind);
bool HasMeasureSpread(const MonteCarloTable &table, std::size_t column);
std::string DescribeUniformReading(const MonteCarloTable &table, std::size_t column);
std::optional<std::string> DescribeUnreadRuns(const MonteCarloTable &table, std::size_t column);
std::optional<SampleStatistics> ComputeSampleStatistics(std::span<const double> samples);
std::size_t SuggestBinCount(std::size_t sample_count);
Histogram BuildHistogram(std::span<const double> samples, std::size_t bin_count);

// Table
std::vector<std::size_t> OrderRuns(const MonteCarloTable &table, const RunSort &sort);
std::vector<std::string> ListMonteCarloWarnings(const MonteCarloTable &table);
std::string WriteMonteCarloCsv(const MonteCarloTable &table, const std::vector<std::string> &column_labels);

} // namespace GUI

#endif // IMCSIM_MONTE_CARLO_RESULTS_H

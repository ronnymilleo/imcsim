/**
 * @file    monte_carlo_results.cpp
 * @brief   What a Monte Carlo analysis keeps of every run: the values drawn, what the run measured, and the statistics
 *          and histogram over the runs.
 */

#include "analysis/monte_carlo_results.h"

#include "plots/trace_statistics.h"
#include "spice/spice_value.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <string_view>

namespace GUI {

namespace {

/// Samples closer than this fraction of their size are the same reading with rounding noise.
constexpr double NOISE_FRACTION = 1e-6;
/// Differences below these are too small to read in the unit of the quantity: a microvolt, a millihertz, a thousandth
/// of a decibel or of a degree, such as the -0.0001 dB of a filter far below its corner.
constexpr double VOLTAGE_FLOOR = 1e-6;
constexpr double FREQUENCY_FLOOR = 1e-3;
constexpr double LOGARITHMIC_FLOOR = 1e-3;
constexpr double LOWER_PERCENTILE = 0.01;
constexpr double UPPER_PERCENTILE = 0.99;
constexpr std::size_t MIN_BIN_COUNT = 5;
constexpr std::size_t MAX_BIN_COUNT = 40;
/// How many failed runs the warning names before it counts the rest.
constexpr std::size_t MAX_LISTED_FAILURES = 10;
constexpr double MIN_HISTOGRAM_HALF_WIDTH = 1e-12;
constexpr double RELATIVE_HISTOGRAM_HALF_WIDTH = 1e-3;

void AddColumn(Measurement &measurement, const int node, const MeasureKind kind, const std::optional<double> value) {
    measurement.Columns.push_back({.Node = node, .Kind = kind});
    measurement.Values.push_back(value);
}

/**
 * @brief   Linear interpolation between the closest samples of the sorted list, as spreadsheets do.
 */
double InterpolatePercentile(const std::vector<double> &sorted, const double fraction) {
    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const std::size_t below = static_cast<std::size_t>(std::floor(position));
    const std::size_t above = std::min(below + 1, sorted.size() - 1);
    return sorted[below] + (sorted[above] - sorted[below]) * (position - static_cast<double>(below));
}

/**
 * @brief   The value to order a run by, or none when it has no such value, which sorts the run last.
 */
std::optional<double> GetSortValue(const MonteCarloRun &run, const RunSort &sort, const std::size_t run_number) {
    switch (sort.By) {
    case RunSort::Key::PART:
        return sort.Index < run.Values.size() ? std::optional<double>(run.Values[sort.Index]) : std::nullopt;
    case RunSort::Key::MEASURE:
        return sort.Index < run.Measures.size() ? run.Measures[sort.Index] : std::nullopt;
    case RunSort::Key::RUN:
    default:
        return static_cast<double>(run_number);
    }
}

std::string QuoteCsvField(const std::string &text) {
    if (text.find_first_of(",\"\n") == std::string::npos) {
        return text;
    }
    std::string quoted = "\"";
    for (const char character : text) {
        quoted += character == '"' ? "\"\"" : std::string(1, character);
    }
    return quoted + "\"";
}

std::string FormatCsvValue(const std::optional<double> value) {
    return value ? std::format("{:.9g}", *value) : std::string();
}

} // namespace

/**
 * @brief   Reads each node of a transient the way the oscilloscope statistics do, over the whole run.
 * @param[in] transient  The result of one run.
 * @return  The mean, RMS, peak to peak and frequency of every node but ground.
 */
Measurement MeasureTransient(const Core::Transient &transient) {
    Measurement measurement;
    for (std::size_t node = 1; node < transient.NodeVoltages.size(); ++node) {
        const std::vector<double> &voltages = transient.NodeVoltages[node];
        std::optional<TraceStatistics> statistics;
        if (voltages.size() == transient.Times.size() && transient.Times.size() >= 2) {
            statistics =
                ComputeTraceStatistics(transient.Times, voltages, transient.Times.front(), transient.Times.back());
        }
        const int number = static_cast<int>(node);
        AddColumn(measurement, number, MeasureKind::MEAN, statistics ? std::optional(statistics->Mean) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::RMS, statistics ? std::optional(statistics->RMS) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::PEAK_TO_PEAK,
                  statistics ? std::optional(statistics->PeakToPeak) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::FREQUENCY, statistics ? statistics->Frequency : std::nullopt);
    }
    return measurement;
}

/**
 * @brief   Reads each node of an AC sweep the way the Bode statistics do, over the whole sweep.
 * @param[in] sweep  The result of one run.
 * @return  The cutoffs, the peak gain, the unity-gain frequency and the phase margin of every node but ground; the
 *          upper cutoff comes first, which is what a low-pass filter is read by.
 */
Measurement MeasureACSweep(const Core::ACSweep &sweep) {
    Measurement measurement;
    for (std::size_t node = 1; node < sweep.NodeMagnitudesDecibels.size(); ++node) {
        const std::vector<double> &magnitudes = sweep.NodeMagnitudesDecibels[node];
        std::optional<BodeStatistics> statistics;
        if (magnitudes.size() == sweep.Frequencies.size() && node < sweep.NodePhasesDegrees.size() &&
            sweep.NodePhasesDegrees[node].size() == magnitudes.size() && magnitudes.size() >= 2) {
            statistics = ComputeBodeStatistics(sweep.Frequencies, magnitudes, sweep.NodePhasesDegrees[node],
                                               sweep.Frequencies.front(), sweep.Frequencies.back());
        }
        const int number = static_cast<int>(node);
        AddColumn(measurement, number, MeasureKind::UPPER_CUTOFF, statistics ? statistics->UpperCutoff : std::nullopt);
        AddColumn(measurement, number, MeasureKind::LOWER_CUTOFF, statistics ? statistics->LowerCutoff : std::nullopt);
        AddColumn(measurement, number, MeasureKind::PEAK_GAIN,
                  statistics ? std::optional(statistics->PeakGain) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::UNITY_GAIN_FREQUENCY,
                  statistics ? statistics->UnityGainFrequency : std::nullopt);
        AddColumn(measurement, number, MeasureKind::PHASE_MARGIN, statistics ? statistics->PhaseMargin : std::nullopt);
    }
    return measurement;
}

/**
 * @brief   Reads each node of a DC sweep the way the curve tracer statistics do, over the whole sweep.
 * @param[in] sweep  The result of one run, whose first curve is read.
 * @return  The extremes and the mean of every node but ground.
 */
Measurement MeasureDCSweep(const Core::DCSweep &sweep) {
    Measurement measurement;
    if (sweep.Curves.empty() || sweep.SweptValues.size() < 2) {
        return measurement;
    }
    const std::vector<std::vector<double>> &voltages = sweep.Curves.front().NodeVoltages;
    for (std::size_t node = 1; node < voltages.size(); ++node) {
        std::optional<SweepStatistics> statistics;
        if (voltages[node].size() == sweep.SweptValues.size()) {
            statistics = ComputeSweepStatistics(sweep.SweptValues, voltages[node], sweep.SweptValues.front(),
                                                sweep.SweptValues.back());
        }
        const int number = static_cast<int>(node);
        AddColumn(measurement, number, MeasureKind::MINIMUM,
                  statistics ? std::optional(statistics->Minimum) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::MAXIMUM,
                  statistics ? std::optional(statistics->Maximum) : std::nullopt);
        AddColumn(measurement, number, MeasureKind::MEAN, statistics ? std::optional(statistics->Mean) : std::nullopt);
    }
    return measurement;
}

/**
 * @brief   Names a measured quantity, as the panel and the table headers show it.
 * @param[in] kind  Quantity to name.
 * @return  Such as "Mean" or "Upper -3 dB frequency".
 */
const char *GetMeasureName(const MeasureKind kind) {
    switch (kind) {
    case MeasureKind::MEAN:
        return "Mean";
    case MeasureKind::RMS:
        return "RMS";
    case MeasureKind::PEAK_TO_PEAK:
        return "Peak to peak";
    case MeasureKind::FREQUENCY:
        return "Frequency";
    case MeasureKind::PEAK_GAIN:
        return "Peak gain";
    case MeasureKind::LOWER_CUTOFF:
        return "Lower -3 dB frequency";
    case MeasureKind::UPPER_CUTOFF:
        return "Upper -3 dB frequency";
    case MeasureKind::UNITY_GAIN_FREQUENCY:
        return "Unity-gain frequency";
    case MeasureKind::PHASE_MARGIN:
        return "Phase margin";
    case MeasureKind::MINIMUM:
        return "Minimum";
    case MeasureKind::MAXIMUM:
        return "Maximum";
    default:
        return "";
    }
}

/**
 * @brief   Gives the unit of a measured quantity.
 * @param[in] kind  Quantity to give the unit of.
 * @return  "V", "Hz", "dB" or the degree sign.
 */
const char *GetMeasureUnit(const MeasureKind kind) {
    switch (kind) {
    case MeasureKind::FREQUENCY:
    case MeasureKind::LOWER_CUTOFF:
    case MeasureKind::UPPER_CUTOFF:
    case MeasureKind::UNITY_GAIN_FREQUENCY:
        return "Hz";
    case MeasureKind::PEAK_GAIN:
        return "dB";
    case MeasureKind::PHASE_MARGIN:
        return "°";
    case MeasureKind::MEAN:
    case MeasureKind::RMS:
    case MeasureKind::PEAK_TO_PEAK:
    case MeasureKind::MINIMUM:
    case MeasureKind::MAXIMUM:
    default:
        return "V";
    }
}

/**
 * @brief   Lists the nodes the table measured.
 * @param[in] table  The runs.
 * @return  Node numbers in rising order, each once.
 */
std::vector<int> ListTableNodes(const MonteCarloTable &table) {
    std::vector<int> nodes;
    for (const MeasureColumn &column : table.Columns) {
        if (!std::ranges::contains(nodes, column.Node)) {
            nodes.push_back(column.Node);
        }
    }
    std::ranges::sort(nodes);
    return nodes;
}

/**
 * @brief   Finds the columns of one node.
 * @param[in] table  The runs.
 * @param[in] node   Node number.
 * @return  Indices into the columns, in the order they were measured, which puts the most telling reading first.
 */
std::vector<std::size_t> ListNodeColumns(const MonteCarloTable &table, const int node) {
    std::vector<std::size_t> columns;
    for (std::size_t index = 0; index < table.Columns.size(); ++index) {
        if (table.Columns[index].Node == node) {
            columns.push_back(index);
        }
    }
    return columns;
}

/**
 * @brief   Picks the quantity to show first.
 * @param[in] table            The runs; it needs at least one column.
 * @param[in] preferred_nodes  Nodes the user measures, in the order they were measured.
 * @return  The column whose drawn runs spread most in proportion to their size (see ComputeRelativeSpread()), so a
 *          quantity that does not vary, such as the gain of the input node, does not hide the corner of the filter.
 *          A tie goes to the node measured last, and within a node to the first quantity the analysis lists; a column
 *          without any reading ranks last.
 */
std::size_t FindDefaultColumn(const MonteCarloTable &table, const std::span<const int> preferred_nodes) {
    const auto rank_of = [&preferred_nodes](const int node) {
        const auto found = std::ranges::find(preferred_nodes, node);
        return found == preferred_nodes.end() ? -1 : static_cast<int>(found - preferred_nodes.begin());
    };
    std::size_t best = 0;
    double best_spread = -1.0;
    int best_rank = -2;
    for (std::size_t column = 0; column < table.Columns.size(); ++column) {
        const std::vector<double> samples = CollectMeasure(table, column);
        const double spread =
            samples.empty() ? -1.0 : ComputeRelativeSpread(samples, GetMeasureFloor(table.Columns[column].Kind));
        const int rank = rank_of(table.Columns[column].Node);
        if (spread > best_spread || (spread == best_spread && rank > best_rank)) {
            best = column;
            best_spread = spread;
            best_rank = rank;
        }
    }
    return best;
}

/**
 * @brief   Gathers what the drawn runs measured in one column.
 * @param[in] table   The runs.
 * @param[in] column  Index of the column.
 * @return  The values of runs 1 and up that did not fail and have the reading; the nominal run is left out, since it
 *          is one circuit and not a draw.
 */
std::vector<double> CollectMeasure(const MonteCarloTable &table, const std::size_t column) {
    std::vector<double> samples;
    for (std::size_t run = 1; run < table.Runs.size(); ++run) {
        const MonteCarloRun &entry = table.Runs[run];
        const std::optional<double> measure = column < entry.Measures.size() ? entry.Measures[column] : std::nullopt;
        if (!entry.IsFailed && measure) {
            samples.push_back(*measure);
        }
    }
    return samples;
}

/**
 * @brief   Tells how much samples spread, in proportion to their size.
 * @param[in] samples         The values.
 * @param[in] absolute_floor  Smallest difference between the extremes that counts, in the unit of the samples.
 * @return  The standard deviation over the largest size among the samples, or 0 for fewer than two samples and for
 *          samples that differ by less than the floor or only by rounding noise, such as 0 dB read as a few
 *          femtodecibels.
 */
double ComputeRelativeSpread(const std::span<const double> samples, const double absolute_floor) {
    if (samples.size() < 2) {
        return 0.0;
    }
    const auto [lowest, highest] = std::ranges::minmax(samples);
    const double size = std::max(std::abs(lowest), std::abs(highest));
    if (highest - lowest <= std::max(absolute_floor, NOISE_FRACTION * size)) {
        return 0.0;
    }
    const std::optional<SampleStatistics> statistics = ComputeSampleStatistics(samples);
    return statistics ? statistics->StandardDeviation / size : 0.0;
}

/**
 * @brief   Gives the smallest difference of a quantity worth reading.
 * @param[in] kind  Quantity.
 * @return  The difference, in the unit of GetMeasureUnit().
 */
double GetMeasureFloor(const MeasureKind kind) {
    const std::string_view unit = GetMeasureUnit(kind);
    if (unit == "Hz") {
        return FREQUENCY_FLOOR;
    }
    return unit == "V" ? VOLTAGE_FLOOR : LOGARITHMIC_FLOOR;
}

/**
 * @brief   Tells whether the drawn runs differ in one column by more than can be read.
 * @param[in] table   The runs.
 * @param[in] column  Index of the column.
 * @return  False when the runs give the same reading, or none has one.
 */
bool HasMeasureSpread(const MonteCarloTable &table, const std::size_t column) {
    return ComputeRelativeSpread(CollectMeasure(table, column), GetMeasureFloor(table.Columns[column].Kind)) > 0.0;
}

/**
 * @brief   Words the reading of a quantity that every drawn run gave alike.
 * @param[in] table   The runs.
 * @param[in] column  Index of the column.
 * @return  Such as "Every run gives 0dB"; the mean of the runs that have the reading.
 */
std::string DescribeUniformReading(const MonteCarloTable &table, const std::size_t column) {
    const std::vector<double> samples = CollectMeasure(table, column);
    const double mean = samples.empty() ? 0.0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    const double shown = std::abs(mean) <= GetMeasureFloor(table.Columns[column].Kind) ? 0.0 : mean;
    return std::format("Every run gives {}{}", Core::FormatValue(shown), GetMeasureUnit(table.Columns[column].Kind));
}

/**
 * @brief   Words the runs that gave no reading of a quantity, such as a cutoff the response never reaches.
 * @param[in] table   The runs.
 * @param[in] column  Index of the column.
 * @return  Such as "Upper -3 dB frequency not reached in 3 runs", or no value when every run that worked has it.
 */
std::optional<std::string> DescribeUnreadRuns(const MonteCarloTable &table, const std::size_t column) {
    std::size_t made = 0;
    for (std::size_t run = 1; run < table.Runs.size(); ++run) {
        made += table.Runs[run].IsFailed ? 0 : 1;
    }
    const std::size_t unread = made - CollectMeasure(table, column).size();
    if (unread == 0) {
        return std::nullopt;
    }
    return std::format("{} not reached in {} {}", GetMeasureName(table.Columns[column].Kind), unread,
                       unread == 1 ? "run" : "runs");
}

/**
 * @brief   Adds up samples.
 * @param[in] samples  The values.
 * @return  The extremes, mean, standard deviation and the 1 % and 99 % percentiles, or no value without samples.
 */
std::optional<SampleStatistics> ComputeSampleStatistics(const std::span<const double> samples) {
    if (samples.empty()) {
        return std::nullopt;
    }
    std::vector<double> sorted(samples.begin(), samples.end());
    std::ranges::sort(sorted);
    const double count = static_cast<double>(sorted.size());
    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / count;
    double squares = 0.0;
    for (const double sample : sorted) {
        squares += (sample - mean) * (sample - mean);
    }
    return SampleStatistics{.Count = sorted.size(),
                            .Minimum = sorted.front(),
                            .Maximum = sorted.back(),
                            .Mean = mean,
                            .StandardDeviation = sorted.size() > 1 ? std::sqrt(squares / (count - 1.0)) : 0.0,
                            .Percentile1 = InterpolatePercentile(sorted, LOWER_PERCENTILE),
                            .Percentile99 = InterpolatePercentile(sorted, UPPER_PERCENTILE)};
}

/**
 * @brief   Picks a number of bins for the histogram.
 * @param[in] sample_count  Samples to show.
 * @return  The square root of the count, which gives a readable shape at any count, kept from 5 to 40.
 */
std::size_t SuggestBinCount(const std::size_t sample_count) {
    const auto root = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(sample_count))));
    return std::clamp(root, MIN_BIN_COUNT, MAX_BIN_COUNT);
}

/**
 * @brief   Counts samples in bins of equal width from the smallest to the largest.
 * @param[in] samples    The values.
 * @param[in] bin_count  Number of bins, at least 1.
 * @return  The range and the count of each bin; the largest sample is in the last bin. When every sample is the same
 *          the range is widened a little around it so the bins have a width.
 */
Histogram BuildHistogram(const std::span<const double> samples, const std::size_t bin_count) {
    Histogram histogram;
    if (samples.empty() || bin_count == 0) {
        return histogram;
    }
    const auto [lowest, highest] = std::ranges::minmax(samples);
    histogram.From = lowest;
    histogram.To = highest;
    if (!(histogram.To > histogram.From)) {
        const double half_width = std::max(std::abs(lowest) * RELATIVE_HISTOGRAM_HALF_WIDTH, MIN_HISTOGRAM_HALF_WIDTH);
        histogram.From = lowest - half_width;
        histogram.To = lowest + half_width;
    }
    histogram.Counts.assign(bin_count, 0);
    const double width = (histogram.To - histogram.From) / static_cast<double>(bin_count);
    for (const double sample : samples) {
        const auto index = static_cast<std::size_t>((sample - histogram.From) / width);
        ++histogram.Counts[std::min(index, bin_count - 1)];
    }
    return histogram;
}

/**
 * @brief   Orders the runs of the table.
 * @param[in] table  The runs.
 * @param[in] sort   What to order by and the direction.
 * @return  Run numbers with the nominal run first, then the others in order; a run without the value to sort by goes
 *          last, whichever the direction.
 */
std::vector<std::size_t> OrderRuns(const MonteCarloTable &table, const RunSort &sort) {
    std::vector<std::size_t> order(table.Runs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    if (order.size() < 2) {
        return order;
    }
    std::ranges::stable_sort(order.begin() + 1, order.end(), [&](const std::size_t left, const std::size_t right) {
        const std::optional<double> left_value = GetSortValue(table.Runs[left], sort, left);
        const std::optional<double> right_value = GetSortValue(table.Runs[right], sort, right);
        if (!left_value || !right_value) {
            return left_value.has_value() && !right_value.has_value();
        }
        return sort.IsAscending ? *left_value < *right_value : *left_value > *right_value;
    });
    return order;
}

/**
 * @brief   Tells what the user should know about the runs: the ones that failed and a stop before the end.
 * @param[in] table  The runs.
 * @return  Sentences for the report, none when every run was made and worked.
 */
std::vector<std::string> ListMonteCarloWarnings(const MonteCarloTable &table) {
    std::vector<std::string> warnings;
    std::vector<std::size_t> failed;
    for (std::size_t run = 1; run < table.Runs.size(); ++run) {
        if (table.Runs[run].IsFailed) {
            failed.push_back(run);
        }
    }
    const std::size_t made = table.Runs.empty() ? 0 : table.Runs.size() - 1;
    if (!failed.empty()) {
        std::string listed;
        for (std::size_t index = 0; index < std::min(failed.size(), MAX_LISTED_FAILURES); ++index) {
            listed += (index == 0 ? "" : ", ") + std::to_string(failed[index]);
        }
        if (failed.size() > MAX_LISTED_FAILURES) {
            listed += std::format(" and {} more", failed.size() - MAX_LISTED_FAILURES);
        }
        warnings.push_back(
            std::format("{} of {} runs failed and are left out of the statistics: {}", failed.size(), made, listed));
    }
    if (made < table.RequestedRuns) {
        warnings.push_back(
            std::format("Stopped after {} of {} runs; the statistics cover those", made, table.RequestedRuns));
    }
    return warnings;
}

/**
 * @brief   Writes the table of runs as CSV.
 * @param[in] table          The runs.
 * @param[in] column_labels  A header for each measured column, such as "V(OUT) Mean (V)".
 * @return  One row per run with its number ("Nominal" for run 0), the value of each part, every measurement and
 *          "ok" or the reason a run failed; a missing reading is an empty field.
 */
std::string WriteMonteCarloCsv(const MonteCarloTable &table, const std::vector<std::string> &column_labels) {
    std::string text = "Run";
    for (std::size_t part = 0; part < table.Parts.size(); ++part) {
        text += "," + QuoteCsvField(std::format("{} ({})", table.Parts[part], table.Units[part]));
    }
    for (const std::string &label : column_labels) {
        text += "," + QuoteCsvField(label);
    }
    text += ",Status\n";
    for (std::size_t run = 0; run < table.Runs.size(); ++run) {
        const MonteCarloRun &entry = table.Runs[run];
        text += run == 0 ? "Nominal" : std::to_string(run);
        for (const double value : entry.Values) {
            text += "," + FormatCsvValue(value);
        }
        for (std::size_t column = 0; column < column_labels.size(); ++column) {
            text += "," + FormatCsvValue(column < entry.Measures.size() ? entry.Measures[column] : std::nullopt);
        }
        text += "," + (entry.IsFailed ? QuoteCsvField(entry.Error) : std::string("ok")) + "\n";
    }
    return text;
}

} // namespace GUI

/**
 * @file    file_measurements.cpp
 * @brief   Writes the measured traces of a schematic to JSON and reads them back.
 */

#include "schematic/file_measurements.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <optional>

namespace GUI {

namespace {

/**
 * @brief   A channel keeps both its operation and its expression, so switching between them after opening loses
 *          nothing.
 */
nlohmann::json WriteMathChannel(const MathChannel &channel) {
    return {
        {"name", channel.Name},
        {"shown", channel.Shown},
        {"uses_expression", channel.UsesExpression},
        {"first", channel.First},
        {"operator", GetOperatorSymbol(channel.Operator)},
        {"second", channel.Second},
        {"expression", channel.Expression},
    };
}

nlohmann::json WriteBus(const LogicBus &bus) {
    return {
        {"name", bus.Name},
        {"shown", bus.Shown},
        {"bits", bus.Bits},
        {"format", GetBusFormatName(bus.Format)},
    };
}

/**
 * @brief   Reads every entry of a list that read_entry accepts.
 * @details Returns false when the key holds no list or some entry was skipped; a missing key is an empty list.
 */
template <typename Value, typename ReadEntry>
bool ReadList(const nlohmann::json &object, const char *key, const ReadEntry &read_entry, std::vector<Value> &values) {
    const auto list = object.find(key);
    if (list == object.end()) {
        return true;
    }
    if (!list->is_array()) {
        return false;
    }
    bool complete = true;
    for (const nlohmann::json &entry : *list) {
        if (const std::optional<Value> value = read_entry(entry)) {
            values.push_back(*value);
        } else {
            complete = false;
        }
    }
    return complete;
}

std::optional<std::string> ReadStringValue(const nlohmann::json &value) {
    if (!value.is_string()) {
        return std::nullopt;
    }
    return value.get<std::string>();
}

/**
 * @brief   A channel needs its name; the other fields fall back to those of a new channel, but an operator that is not
 *          one of the four rejects the channel, since it would compute something else.
 */
std::optional<MathChannel> ReadMathChannelValue(const nlohmann::json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> name = ReadString(value, "name");
    if (!name || name->empty()) {
        return std::nullopt;
    }
    MathChannel channel{.Name = *name};
    channel.Shown = ReadBool(value, "shown").value_or(channel.Shown);
    channel.UsesExpression = ReadBool(value, "uses_expression").value_or(channel.UsesExpression);
    channel.First = ReadString(value, "first").value_or("");
    channel.Second = ReadString(value, "second").value_or("");
    channel.Expression = ReadString(value, "expression").value_or("");
    if (const std::optional<std::string> symbol = ReadString(value, "operator")) {
        const std::optional<MathOperator> math_operator = ParseOperatorSymbol(*symbol);
        if (!math_operator) {
            return std::nullopt;
        }
        channel.Operator = *math_operator;
    }
    return channel;
}

/**
 * @brief   A bus needs its name and a list of bit names; a format it does not know rejects it, since it would show
 *          other values.
 */
std::optional<LogicBus> ReadBusValue(const nlohmann::json &value) {
    if (!value.is_object()) {
        return std::nullopt;
    }
    const std::optional<std::string> name = ReadString(value, "name");
    const auto bits = value.find("bits");
    if (!name || name->empty() || bits == value.end() || !bits->is_array()) {
        return std::nullopt;
    }
    LogicBus bus{.Name = *name};
    bus.Shown = ReadBool(value, "shown").value_or(bus.Shown);
    for (const nlohmann::json &bit : *bits) {
        if (!bit.is_string()) {
            return std::nullopt;
        }
        bus.Bits.push_back(bit.get<std::string>());
    }
    if (const std::optional<std::string> format_name = ReadString(value, "format")) {
        const std::optional<BusFormat> format = ParseBusFormat(*format_name);
        if (!format) {
            return std::nullopt;
        }
        bus.Format = *format;
    }
    return bus;
}

} // namespace

/**
 * @brief   Writes the traces the plots show.
 * @param[in] measurements  Traces to write; the lists that are empty are left out.
 * @return  An object with the voltages and currents, and the math channels and buses when there are any.
 */
nlohmann::json WriteMeasurements(const SavedMeasurements &measurements) {
    nlohmann::json voltages = nlohmann::json::array();
    for (const GridPoint point : measurements.Voltages) {
        voltages.push_back({point.X, point.Y});
    }
    nlohmann::json measurements_object = {{"voltages", voltages}, {"currents", measurements.Currents}};
    if (!measurements.MathChannels.empty()) {
        nlohmann::json channels = nlohmann::json::array();
        for (const MathChannel &channel : measurements.MathChannels) {
            channels.push_back(WriteMathChannel(channel));
        }
        measurements_object["math"] = channels;
    }
    if (!measurements.Buses.empty()) {
        nlohmann::json buses = nlohmann::json::array();
        for (const LogicBus &bus : measurements.Buses) {
            buses.push_back(WriteBus(bus));
        }
        measurements_object["buses"] = buses;
    }
    return measurements_object;
}

/**
 * @brief   Reads the traces the plots show.
 * @details Entries that cannot be read are skipped with one warning per list, since a lost trace is easy to probe
 *          again.
 * @param[in]     document  Document object that holds the "measurements" key.
 * @param[in,out] warnings  Receives one message for each list with entries skipped.
 * @return  The traces read; none when the file has no measurements.
 */
SavedMeasurements ReadMeasurements(const nlohmann::json &document, std::vector<std::string> &warnings) {
    SavedMeasurements measurements;
    const auto object = document.find("measurements");
    if (object == document.end()) {
        return measurements;
    }
    if (!object->is_object()) {
        warnings.push_back("The measurements are not an object and were skipped");
        return measurements;
    }
    if (!ReadList(*object, "voltages", ReadPointValue, measurements.Voltages)) {
        warnings.push_back("Some measured voltages are not integer points and were skipped");
    }
    if (!ReadList(*object, "currents", ReadStringValue, measurements.Currents)) {
        warnings.push_back("Some measured currents are not names and were skipped");
    }
    if (!ReadList(*object, "math", ReadMathChannelValue, measurements.MathChannels)) {
        warnings.push_back("Some math channels could not be read and were skipped");
    }
    if (!ReadList(*object, "buses", ReadBusValue, measurements.Buses)) {
        warnings.push_back("Some logic buses could not be read and were skipped");
    }
    return measurements;
}

} // namespace GUI

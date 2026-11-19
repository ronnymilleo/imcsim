/**
 * @file    file_part_state_fields.cpp
 * @brief   Writes and reads the state that only some kinds of part carry in the schematic file.
 */

#include "schematic/file_part_state_fields.h"

#include "circuit/components/behavioral_source.h"
#include "circuit/components/controlled_source.h"
#include "circuit/components/logic_input.h"
#include "circuit/components/net_label.h"
#include "circuit/components/potentiometer.h"
#include "circuit/components/subcircuit.h"
#include "circuit/components/subcircuit_pin.h"
#include "circuit/components/switch.h"
#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <array>
#include <optional>

namespace GUI {

namespace {

// Each pair fills what one kind of part carries, and does nothing for the others; a missing key keeps the default

/**
 * @brief   The current a controlled source follows; a missing one loads as none, and the simulation then asks for one.
 */
void WriteControllingCurrent(const Core::Component &component, nlohmann::json &object) {
    if (Core::IsCurrentControlled(component.GetType())) {
        object["control"] = static_cast<const Core::ControlledSource &>(component).GetControllingCurrent();
    }
}

std::expected<void, std::string> ReadControllingCurrent(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsCurrentControlled(component.GetType())) {
        return {};
    }
    std::string current;
    const std::expected<bool, std::string> has_current =
        ReadOptionalString(object, "control", current, "has a controlling current that is not text");
    if (!has_current) {
        return std::unexpected(has_current.error());
    }
    if (*has_current) {
        static_cast<Core::ControlledSource &>(component).SetControllingCurrent(current);
    }
    return {};
}

/**
 * @brief   The expression of a behavioral source, kept as written; a missing one loads as the default.
 */
void WriteBehavioralExpression(const Core::Component &component, nlohmann::json &object) {
    if (Core::IsBehavioralSource(component.GetType())) {
        object["expression"] = static_cast<const Core::BehavioralSource &>(component).GetExpression();
    }
}

std::expected<void, std::string> ReadBehavioralExpression(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsBehavioralSource(component.GetType())) {
        return {};
    }
    std::string expression;
    const std::expected<bool, std::string> has_expression =
        ReadOptionalString(object, "expression", expression, "has an expression that is not text");
    if (!has_expression) {
        return std::unexpected(has_expression.error());
    }
    if (*has_expression) {
        static_cast<Core::BehavioralSource &>(component).SetExpression(expression);
    }
    return {};
}

/**
 * @brief   The net of a label or of a pin marker.
 */
void WriteNetName(const Core::Component &component, nlohmann::json &object) {
    const Core::ComponentType type = component.GetType();
    if (type != Core::ComponentType::NET_LABEL && !Core::IsSubcircuitPin(type)) {
        return;
    }
    // A label or pin marker with no name yet is saved without the key
    if (const std::string &net = static_cast<const Core::NetLabel &>(component).GetNet(); !net.empty()) {
        object["net"] = net;
    }
}

std::expected<void, std::string> ReadNetName(const nlohmann::json &object, Core::Component &component) {
    const Core::ComponentType type = component.GetType();
    if ((type != Core::ComponentType::NET_LABEL && !Core::IsSubcircuitPin(type)) || !object.contains("net")) {
        return {};
    }
    const std::optional<std::string> net = ReadString(object, "net");
    const bool is_pin = Core::IsSubcircuitPin(type);
    const bool valid =
        net && (is_pin ? Core::SubcircuitInstance::IsValidIdentifier(*net) : Core::NetLabel::IsValidNet(*net));
    if (!valid) {
        return std::unexpected(is_pin ? "has a subcircuit pin name that is not an identifier"
                                      : "has a net name that is not 1 to 32 letters, digits, _, - or .");
    }
    static_cast<Core::NetLabel &>(component).SetNet(*net);
    return {};
}

/**
 * @brief   A marker that is a port saves its number and reference impedance; one that is not saves nothing, and a pin
 *          marker saved without a port is no port.
 */
void WritePortSettings(const Core::Component &component, nlohmann::json &object) {
    if (!Core::IsSubcircuitPin(component.GetType())) {
        return;
    }
    const Core::PortSettings &port = static_cast<const Core::SubcircuitPinMarker &>(component).GetPort();
    if (port.IsPort) {
        object["port"] = {{"number", port.Number}, {"z0", port.ReferenceOhms}};
    }
}

std::expected<void, std::string> ReadPortSettings(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsSubcircuitPin(component.GetType()) || !object.contains("port")) {
        return {};
    }
    const nlohmann::json &port = object["port"];
    const std::optional<int> number = port.is_object() ? ReadInt(port, "number") : std::nullopt;
    const std::optional<double> reference = port.is_object() ? ReadNumber(port, "z0") : std::nullopt;
    if (!number || !reference || *number < 1 || *reference <= 0.0) {
        return std::unexpected("has an S-parameter port without a valid number and reference impedance");
    }
    static_cast<Core::SubcircuitPinMarker &>(component).SetPort(
        {.IsPort = true, .Number = *number, .ReferenceOhms = *reference});
    return {};
}

/**
 * @brief   A switch saved without a state stays open.
 */
void WriteSwitchState(const Core::Component &component, nlohmann::json &object) {
    if (Core::IsSwitch(component.GetType())) {
        object["closed"] = static_cast<const Core::Switch &>(component).IsClosed();
    }
}

std::expected<void, std::string> ReadSwitchState(const nlohmann::json &object, Core::Component &component) {
    if (!Core::IsSwitch(component.GetType())) {
        return {};
    }
    bool is_closed = false;
    const std::expected<bool, std::string> has_state =
        ReadOptionalBool(object, "closed", is_closed, "has a closed flag that is not true or false");
    if (!has_state) {
        return std::unexpected(has_state.error());
    }
    if (*has_state) {
        static_cast<Core::Switch &>(component).SetClosed(is_closed);
    }
    return {};
}

/**
 * @brief   A potentiometer saved without a wiper keeps it in the middle.
 */
void WriteWiper(const Core::Component &component, nlohmann::json &object) {
    if (component.GetType() == Core::ComponentType::POTENTIOMETER) {
        object["wiper"] = static_cast<const Core::Potentiometer &>(component).GetWiper();
    }
}

std::expected<void, std::string> ReadWiper(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::POTENTIOMETER) {
        return {};
    }
    double wiper = 0.0;
    const std::expected<bool, std::string> has_wiper =
        ReadOptionalNumber(object,
                           "wiper",
                           wiper,
                           "has a wiper that is not a number from 0 to 1",
                           &Core::Potentiometer::IsValidWiper);
    if (!has_wiper) {
        return std::unexpected(has_wiper.error());
    }
    if (*has_wiper) {
        static_cast<Core::Potentiometer &>(component).SetWiper(wiper);
    }
    return {};
}

/**
 * @brief   A logic input saved without a state stays low.
 */
void WriteLogicInputState(const Core::Component &component, nlohmann::json &object) {
    if (component.GetType() == Core::ComponentType::LOGIC_INPUT) {
        object["high"] = static_cast<const Core::LogicInput &>(component).IsHigh();
    }
}

std::expected<void, std::string> ReadLogicInputState(const nlohmann::json &object, Core::Component &component) {
    if (component.GetType() != Core::ComponentType::LOGIC_INPUT) {
        return {};
    }
    bool is_high = false;
    const std::expected<bool, std::string> has_state =
        ReadOptionalBool(object, "high", is_high, "has a high flag that is not true or false");
    if (!has_state) {
        return std::unexpected(has_state.error());
    }
    if (*has_state) {
        static_cast<Core::LogicInput &>(component).SetHigh(is_high);
    }
    return {};
}

/**
 * @struct  StateCodec
 * @brief   The functions that write and read one piece of state, side by side so a key is never added to one only.
 */
struct StateCodec {
    void (*Write)(const Core::Component &, nlohmann::json &);
    std::expected<void, std::string> (*Read)(const nlohmann::json &, Core::Component &);
};

constexpr auto STATE_CODECS = std::to_array<StateCodec>({
    {&WriteControllingCurrent, &ReadControllingCurrent},
    {&WriteBehavioralExpression, &ReadBehavioralExpression},
    {&WriteNetName, &ReadNetName},
    {&WritePortSettings, &ReadPortSettings},
    {&WriteSwitchState, &ReadSwitchState},
    {&WriteWiper, &ReadWiper},
    {&WriteLogicInputState, &ReadLogicInputState},
});

} // namespace

/**
 * @brief   Writes the state of the parts that have one, such as the current a controlled source follows, the
 *          expression of a behavioral source, a net name or a switch position.
 * @param[in]     component  Part to write.
 * @param[in,out] object     Element object that receives the keys.
 */
void WriteStateFields(const Core::Component &component, nlohmann::json &object) {
    for (const StateCodec &codec : STATE_CODECS) {
        codec.Write(component, object);
    }
}

/**
 * @brief   Reads what only some kinds of part carry: the current a controlled source follows, the expression of a
 *          behavioral source, a net name, a port, a switch state, a wiper and the state of a logic input.
 * @param[in]     object     Element object to read.
 * @param[in,out] component  Part that receives the state.
 * @return  Nothing on success, or what is wrong with the state.
 */
std::expected<void, std::string> ReadStateFields(const nlohmann::json &object, Core::Component &component) {
    for (const StateCodec &codec : STATE_CODECS) {
        if (const auto read = codec.Read(object, component); !read) {
            return read;
        }
    }
    return {};
}

} // namespace GUI

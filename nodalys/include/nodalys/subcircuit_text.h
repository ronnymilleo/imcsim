/**
 * @file    subcircuit_text.h
 * @brief   Adds a subcircuit written as SPICE text, such as a vendor's op-amp model, to a circuit being built.
 * @details The text is what lies between a .subckt line and its .ends. It may hold elements (R, C, L, V, I, E, G, H,
 *          F, B, D, Q, M, S and X), .param lines, .model cards and nested .subckt definitions, with numbers written as
 *          literals or as {expressions} of parameters. Each instance gets private internal nodes and element names
 *          that start with its own prefix, so two instances of one text never meet. What the engine cannot simulate,
 *          such as a JFET, a .func or a MOSFET of a level other than 1, is refused with a message
 *          that names the subcircuit and the line, never skipped.
 */

#ifndef NODALYS_SUBCIRCUIT_TEXT_H
#define NODALYS_SUBCIRCUIT_TEXT_H

#include "nodalys/circuit_builder.h"
#include "nodalys/types.h"

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Nodalys {

/**
 * @struct  SubcircuitText
 * @brief   A subcircuit as a library file gives it.
 */
struct SubcircuitText {
    std::string Name;
    /// The node names of the .subckt line, in order.
    std::vector<std::string> Ports;
    /// Everything between the .subckt line and its .ends, the .param and .model definitions it uses included.
    std::string Body;
};

/**
 * @struct  SubcircuitTextInstance
 * @brief   One use of a subcircuit text.
 */
struct SubcircuitTextInstance {
    /// Put before the name of each element, such as "X1" for "X1.R1", so that the elements of two instances differ.
    std::string Prefix;
    /// The node of the circuit each port is connected to, in the order of the ports.
    std::vector<NodeId> PortNodes;
    /// The value of each parameter of the .subckt line, defaults and overrides settled; they win over a .param line of
    /// the text with the same name.
    std::map<std::string, double> Parameters;
    /// Called, when set, with the name of each internal node an instance makes and the node it got, such as "X1.MID"
    /// (upper case, the names of the instances it sits in before it), so the caller can let its own directives name it.
    std::function<void(const std::string &name, NodeId node)> OnInternalNode;
    /// Called, when set, with the name and the two nodes of each independent source, such as "X1.V1".
    std::function<void(const std::string &name, NodeId positive, NodeId negative)> OnSource;
};

// Building
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
AddSubcircuitText(CircuitBuilder &builder, const SubcircuitText &text, const SubcircuitTextInstance &instance);
[[nodiscard]] std::optional<std::string> CheckSubcircuitText(const SubcircuitText &text,
                                                             const std::map<std::string, double> &parameters);

} // namespace Nodalys

#endif // NODALYS_SUBCIRCUIT_TEXT_H

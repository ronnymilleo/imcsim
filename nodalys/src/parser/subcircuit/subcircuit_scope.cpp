/**
 * @file    subcircuit_scope.cpp
 * @brief   What one instance of a subcircuit kept as SPICE text sees while it is expanded: its parameters, models,
 *          nested subcircuits and nodes.
 */

#include "parser/subcircuit/subcircuit_scope.h"

#include "parser/syntax/spice_text.h"
#include "parser/syntax/spice_values.h"
#include "text/text_case.h"

#include <format>

namespace Nodalys {

/**
 * @param[in,out] context  Expansion the scope belongs to, which gives its new nodes and collects what it reports. It
 *                         must outlive this scope; a child scope is made with the same one.
 * @param[in]     parent   Scope the definition of this instance sits in, or null for the instance the expansion starts
 *                         from. It must outlive this scope.
 * @param[in]     prefix   Name of the instance with those it sits in, such as "X1.XLOAD", put before its element names
 *                         so two instances of one block never share one.
 */
TextScope::TextScope(ExpansionContext &context, const TextScope *parent, std::string prefix)
    : m_Context(context), m_Parent(parent), m_Prefix(std::move(prefix)) {
}

/**
 * @brief   Returns the expansion context this scope belongs to, shared by all scopes of one expansion.
 * @return  Mutable reference to the shared ExpansionContext.
 */
ExpansionContext &TextScope::GetContext() const {
    return m_Context;
}

/**
 * @brief   Finds a parameter in this scope or the ones its definition sits in.
 * @param[in] upper_name  Upper-case name.
 * @return  The value, or none when no scope has the name.
 */
std::optional<double> TextScope::FindParameter(const std::string &upper_name) const {
    if (const auto found = m_Parameters.find(upper_name); found != m_Parameters.end()) {
        return found->second;
    }
    return m_Parent != nullptr ? m_Parent->FindParameter(upper_name) : std::nullopt;
}

/**
 * @brief   Binds a parameter value directly in this scope.
 * @param[in] upper_name  Upper-case parameter name.
 * @param[in] value       Numeric parameter value.
 */
void TextScope::SetParameter(const std::string &upper_name, const double value) {
    m_Parameters[upper_name] = value;
}

/**
 * @brief   Tells whether this scope itself holds a parameter, which a `.param` line of the text must not replace: the
 *          value the instance passed wins over the default the text gives.
 * @param[in] upper_name  Upper-case parameter name.
 * @return  True if this scope owns a binding for `upper_name`.
 */
bool TextScope::HasOwnParameter(const std::string &upper_name) const {
    return m_Parameters.contains(upper_name);
}

/**
 * @brief   Evaluates a number or an expression of the parameters this scope sees.
 * @param[in] text  SPICE literal or braced/quoted parameter expression.
 * @return  Evaluated numeric value, or a parse/evaluation error message.
 */
std::expected<double, std::string> TextScope::Evaluate(const std::string_view text) const {
    return EvaluateValue(text, [this](const std::string &name) { return FindParameter(name); });
}

/**
 * @brief   Registers a `.model` definition in this scope.
 * @param[in] upper_name  Upper-case model name.
 * @param[in] model       Parsed model text definition.
 */
void TextScope::AddModel(const std::string &upper_name, TextModel model) {
    m_Models[upper_name] = std::move(model);
}

/**
 * @brief   Finds a model in this scope or the ones its definition sits in.
 * @param[in] upper_name  Upper-case model name.
 * @return  The model, or null when no scope has the name.
 */
const TextModel *TextScope::FindModel(const std::string &upper_name) const {
    if (const auto found = m_Models.find(upper_name); found != m_Models.end()) {
        return &found->second;
    }
    return m_Parent != nullptr ? m_Parent->FindModel(upper_name) : nullptr;
}

/**
 * @brief   Registers a nested `.subckt` definition in this scope.
 * @param[in] upper_name  Upper-case subcircuit name.
 * @param[in] subcircuit  Subcircuit definition text.
 */
void TextScope::AddSubcircuit(const std::string &upper_name, TextSubcircuit subcircuit) {
    m_Subcircuits[upper_name] = std::move(subcircuit);
}

/**
 * @brief   Finds a subcircuit written in this scope or in the ones its definition sits in.
 * @param[in] upper_name  Upper-case subcircuit name.
 * @return  The subcircuit, or null when no scope has the name.
 */
const TextSubcircuit *TextScope::FindSubcircuit(const std::string &upper_name) const {
    if (const auto found = m_Subcircuits.find(upper_name); found != m_Subcircuits.end()) {
        return &found->second;
    }
    return m_Parent != nullptr ? m_Parent->FindSubcircuit(upper_name) : nullptr;
}

/**
 * @brief   Makes a name of the text stand for a node of the circuit, as a port does.
 * @param[in] upper_name  Upper-case local node name.
 * @param[in] node        Circuit NodeId mapped to `upper_name`.
 */
void TextScope::MapNode(const std::string &upper_name, const NodeId node) {
    m_Nodes[upper_name] = node;
}

/**
 * @brief   The node a name of the text stands for.
 * @details Ground is 0 or GND. Any other name that no port mapped is private to this scope and gets a node of its own
 *          the first time it is read.
 * @param[in] name  Node name as written.
 * @return  The node.
 */
NodeId TextScope::ResolveNode(const std::string_view name) {
    const std::string upper = ToUpper(TrimSpaces(name));
    if (IsGroundName(upper)) {
        return GROUND_NODE;
    }
    if (const auto found = m_Nodes.find(upper); found != m_Nodes.end()) {
        return found->second;
    }
    const NodeId node = m_Context.Builder.AddNode();
    m_Nodes[upper] = node;
    if (const InstanceObservers &observers = m_Context.Observers; observers.OnInternalNode) {
        observers.OnInternalNode(ToUpper(MakeName(upper)), node);
    }
    return node;
}

/**
 * @brief   Remembers a voltage source of this scope, which a controlled source or an expression names to read its
 *          current.
 * @param[in] upper_name    Upper-case local source name.
 * @param[in] written_name  Original-case local source name.
 */
void TextScope::AddSourceName(const std::string &upper_name, std::string written_name) {
    m_SourceNames[upper_name] = std::move(written_name);
}

/**
 * @brief   Finds the full name of a voltage source of this scope.
 * @param[in] upper_name  Upper-case local source name.
 * @return  The name the circuit knows it by, or none when the scope has no such source.
 */
std::optional<std::string> TextScope::FindSourceName(const std::string &upper_name) const {
    if (const auto found = m_SourceNames.find(upper_name); found != m_SourceNames.end()) {
        return MakeName(found->second);
    }
    return std::nullopt;
}

/**
 * @brief   Builds the hierarchical name an element of this scope has in the circuit.
 * @param[in] local_name  Element name as written inside the subcircuit.
 * @return  Prefixed element name (or `local_name` at top level).
 */
std::string TextScope::MakeName(const std::string_view local_name) const {
    return m_Prefix.empty() ? std::string(local_name) : std::format("{}.{}", m_Prefix, local_name);
}

/**
 * @brief   Returns the hierarchical instance prefix of this scope.
 * @return  Const reference to the prefix string.
 */
const std::string &TextScope::GetPrefix() const {
    return m_Prefix;
}

} // namespace Nodalys

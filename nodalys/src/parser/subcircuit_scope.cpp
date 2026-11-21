/**
 * @file    subcircuit_scope.cpp
 * @brief   What one instance of a subcircuit kept as SPICE text sees while it is expanded: its parameters, models,
 *          nested subcircuits and nodes.
 */

#include "parser/subcircuit_scope.h"

#include "parser/spice_text.h"
#include "parser/spice_values.h"
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
 * @brief   The expansion this scope belongs to, shared by all the scopes of one expansion.
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

void TextScope::SetParameter(const std::string &upper_name, const double value) {
    m_Parameters[upper_name] = value;
}

/**
 * @brief   Tells whether this scope itself holds a parameter, which a .param line of the text must not replace: the
 *          value the instance passed wins over the default the text gives.
 */
bool TextScope::HasOwnParameter(const std::string &upper_name) const {
    return m_Parameters.contains(upper_name);
}

/**
 * @brief   Evaluates a number or an expression of the parameters this scope sees.
 */
std::expected<double, std::string> TextScope::Evaluate(const std::string_view text) const {
    return EvaluateValue(text, [this](const std::string &name) { return FindParameter(name); });
}

void TextScope::AddModel(const std::string &upper_name, TextModel model) {
    m_Models[upper_name] = std::move(model);
}

/**
 * @brief   Finds a model in this scope or the ones its definition sits in.
 * @return  The model, or null when no scope has the name.
 */
const TextModel *TextScope::FindModel(const std::string &upper_name) const {
    if (const auto found = m_Models.find(upper_name); found != m_Models.end()) {
        return &found->second;
    }
    return m_Parent != nullptr ? m_Parent->FindModel(upper_name) : nullptr;
}

void TextScope::AddSubcircuit(const std::string &upper_name, TextSubcircuit subcircuit) {
    m_Subcircuits[upper_name] = std::move(subcircuit);
}

/**
 * @brief   Finds a subcircuit written in this scope or in the ones its definition sits in.
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
 */
void TextScope::AddSourceName(const std::string &upper_name, std::string written_name) {
    m_SourceNames[upper_name] = std::move(written_name);
}

/**
 * @brief   Finds the full name of a voltage source of this scope.
 * @return  The name the circuit knows it by, or none when the scope has no such source.
 */
std::optional<std::string> TextScope::FindSourceName(const std::string &upper_name) const {
    if (const auto found = m_SourceNames.find(upper_name); found != m_SourceNames.end()) {
        return MakeName(found->second);
    }
    return std::nullopt;
}

/**
 * @brief   The name an element of this scope has in the circuit: its own name after that of the instance.
 */
std::string TextScope::MakeName(const std::string_view local_name) const {
    return m_Prefix.empty() ? std::string(local_name) : std::format("{}.{}", m_Prefix, local_name);
}

const std::string &TextScope::GetPrefix() const {
    return m_Prefix;
}

} // namespace Nodalys

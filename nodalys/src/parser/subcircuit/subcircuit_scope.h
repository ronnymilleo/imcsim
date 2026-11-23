/**
 * @file    subcircuit_scope.h
 * @brief   What one instance of a subcircuit kept as SPICE text sees while it is expanded: its parameters, models,
 *          nested subcircuits and nodes.
 */

#ifndef NODALYS_PARSER_SUBCIRCUIT_SCOPE_H
#define NODALYS_PARSER_SUBCIRCUIT_SCOPE_H

#include "nodalys/circuit_builder.h"
#include "nodalys/subcircuit_text.h"
#include "nodalys/types.h"
#include "parser/syntax/model_cards.h"

#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Nodalys {

class TextScope;

/**
 * @struct  TextModel
 * @brief   A .model card of the text, with its parameters evaluated, kept until an element uses it.
 */
struct TextModel {
    std::string Name;
    std::string TypeName; ///< The type as the card writes it, upper case, such as "D" or "NJF".
    /// False when the engine has no device for the type, which refuses an element that uses the model.
    bool IsSupported = false;
    ParsedModel Card;
};

/**
 * @struct  TextSubcircuit
 * @brief   A .subckt written inside the text, expanded once for each X line that uses it.
 */
struct TextSubcircuit {
    std::string Name;
    std::vector<std::string> Ports;
    /// Upper-case parameter name and the text of its default, in the order of the .subckt line.
    std::vector<std::pair<std::string, std::string>> Parameters;
    std::vector<std::string> Lines;
    /// The scope the definition sits in, which its models, subcircuits and parameters are looked up from.
    const TextScope *DefinedIn = nullptr;
};

/**
 * @struct  ExpansionContext
 * @brief   What one expansion shares between all its scopes: the builder it adds to and what it reports.
 */
struct ExpansionContext {
    CircuitBuilder &Builder;
    std::string SubcircuitName; ///< Name of the subcircuit that was asked for, for the messages.
    std::vector<std::string> Warnings;
    /// Upper-case names of the models whose ignored parameters were already reported, so each is named once however
    /// often it is used.
    std::set<std::string> ReportedModels;
    InstanceObservers Observers; ///< What the caller wants to hear about the nodes and sources the expansion makes.
};

/**
 * @class   TextScope
 * @brief   One instance of a subcircuit, or the instance the whole expansion starts from.
 * @details Names are looked up in the scope and then in the scope the definition sits in, so a block sees the models,
 *          parameters and subcircuits of the text around its definition, as in SPICE. Nodes are private to the scope
 *          unless a port maps them to the scope above.
 */
class TextScope {
public:
    TextScope(ExpansionContext &context, const TextScope *parent, std::string prefix);

    // Expansion
    ExpansionContext &GetContext() const;

    // Parameters
    [[nodiscard]] std::optional<double> FindParameter(const std::string &upper_name) const;
    void SetParameter(const std::string &upper_name, double value);
    bool HasOwnParameter(const std::string &upper_name) const;
    [[nodiscard]] std::expected<double, std::string> Evaluate(std::string_view text) const;

    // Definitions
    void AddModel(const std::string &upper_name, TextModel model);
    const TextModel *FindModel(const std::string &upper_name) const;
    void AddSubcircuit(const std::string &upper_name, TextSubcircuit subcircuit);
    const TextSubcircuit *FindSubcircuit(const std::string &upper_name) const;

    // Nodes and names
    void MapNode(const std::string &upper_name, NodeId node);
    NodeId ResolveNode(std::string_view name);
    void AddSourceName(const std::string &upper_name, std::string written_name);
    [[nodiscard]] std::optional<std::string> FindSourceName(const std::string &upper_name) const;
    std::string MakeName(std::string_view local_name) const;
    const std::string &GetPrefix() const;

private:
    ExpansionContext &m_Context;
    const TextScope *m_Parent;
    std::string m_Prefix;
    std::map<std::string, double> m_Parameters;
    std::map<std::string, TextModel> m_Models;
    std::map<std::string, TextSubcircuit> m_Subcircuits;
    std::map<std::string, NodeId> m_Nodes;
    std::map<std::string, std::string> m_SourceNames;
};

} // namespace Nodalys

#endif // NODALYS_PARSER_SUBCIRCUIT_SCOPE_H

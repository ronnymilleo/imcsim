/**
 * @file    bjt.h
 * @brief   NPN and PNP bipolar transistors, simulated with a model from a fixed list or with custom parameters.
 */

#ifndef IMCSIM_BJT_H
#define IMCSIM_BJT_H

#include "components/component.h"
#include <memory>
#include <nodalys/types.h>
#include <span>
#include <string>
#include <string_view>

namespace Core {

using BJTParameters = Nodalys::BjtParameters;

/**
 * @struct  BJTModel
 * @brief   A ready SPICE transistor model, offered for NPN or PNP parts.
 * @details Name identifies the model in files and in the editor; SpiceName names its ".model" line.
 */
struct BJTModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    BJTParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CUSTOM_BJT_MODEL_NAME = "Custom";

/**
 * @class   BJT
 * @brief   A bipolar transistor with collector, base and emitter terminals, in that order.
 * @details The same class serves NPN and PNP parts; the component type tells which models it can use. It has no
 *          numeric value: a ready model or its own custom parameters set its behavior.
 */
class BJT : public Component {
public:
    explicit BJT(ComponentType type);
    ~BJT() override = default;

    // Copying
    std::unique_ptr<Component> Clone() const override;

    // Ready model
    const BJTModel *GetModel() const;
    void SetModel(const BJTModel &model);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const BJTParameters &parameters);
    bool IsValidParameters(const BJTParameters &parameters) const;

    // Simulation
    const BJTParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    /// Null when the transistor uses its custom parameters.
    const BJTModel *m_Model;
    BJTParameters m_CustomParameters;
};

// Bipolar transistors
bool IsBJT(ComponentType type);
std::span<const BJTModel> GetBJTModels(ComponentType type);
const BJTModel *FindBJTModel(ComponentType type, std::string_view name);
std::string FormatBJTModel(ComponentType type, std::string_view spice_name, const BJTParameters &parameters);

} // namespace Core

#endif // IMCSIM_BJT_H

/**
 * @file    mosfet.h
 * @brief   N- and P-channel MOSFETs, simulated with a model from a fixed list or with custom parameters.
 */

#ifndef IMCSIM_MOSFET_H
#define IMCSIM_MOSFET_H

#include "components/component.h"
#include <memory>
#include <nodalys/types.h>
#include <span>
#include <string>
#include <string_view>

namespace Core {

using MOSFETParameters = nodalys::MosfetParameters;

/**
 * @struct  MOSFETModel
 * @brief   A ready SPICE MOSFET model, offered for NMOS or PMOS parts.
 * @details Name identifies the model in files and in the editor; SpiceName names its ".model" line.
 */
struct MOSFETModel {
    const char *Name;
    const char *Description;
    const char *SpiceName;
    MOSFETParameters Parameters;
    ComponentType Type;
};

inline constexpr const char *CUSTOM_MOSFET_MODEL_NAME = "Custom";

/**
 * @class   MOSFET
 * @brief   An enhancement MOSFET with drain, gate and source terminals, in that order, and its body tied to the
 *          source.
 * @details The same class serves NMOS and PMOS parts; the component type tells which models it can use. It has no
 *          numeric value: a ready model or its own custom parameters set its behavior.
 */
class MOSFET : public Component {
public:
    explicit MOSFET(ComponentType type);
    ~MOSFET() override = default;

    // Copying
    std::unique_ptr<Component> Clone() const override;

    // Ready model
    const MOSFETModel *GetModel() const;
    void SetModel(const MOSFETModel &model);

    // Custom parameters
    bool IsCustom() const;
    void SetCustom();
    void SetCustomParameters(const MOSFETParameters &parameters);
    bool IsValidParameters(const MOSFETParameters &parameters) const;

    // Simulation
    const MOSFETParameters &GetParameters() const;
    const char *GetModelName() const;
    std::string GetSpiceModelName() const;

private:
    /// Null when the transistor uses its custom parameters.
    const MOSFETModel *m_Model;
    MOSFETParameters m_CustomParameters;
};

// MOSFETs
bool IsMOSFET(ComponentType type);
std::span<const MOSFETModel> GetMOSFETModels(ComponentType type);
const MOSFETModel *FindMOSFETModel(ComponentType type, std::string_view name);
std::string FormatMOSFETModel(ComponentType type, std::string_view spice_name, const MOSFETParameters &parameters);

} // namespace Core

#endif // IMCSIM_MOSFET_H

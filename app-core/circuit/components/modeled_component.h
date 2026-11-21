/**
 * @file    modeled_component.h
 * @brief   A part simulated with a ready model from its list, its own parameters or a model from an imported library,
 *          as the diodes and the transistors are.
 */

#ifndef IMCSIM_MODELED_COMPONENT_H
#define IMCSIM_MODELED_COMPONENT_H

#include "circuit/components/component.h"
#include "circuit/components/model_choice.h"
#include "circuit/library_model.h"
#include <format>
#include <string>

namespace Core {

/**
 * @class   ModeledComponent
 * @brief   The model functions that every diode and transistor shares, over a ModelChoice and the library model that
 *          every component carries.
 * @details A library model wins over the choice while it is set; picking a ready model or custom parameters clears
 *          it. Model must have the members Name, SpiceName and Parameters, of type Parameters. Each part keeps its own
 *          list of models, its check of the parameters and the prefix of the ".model" name of a custom part, such as
 *          "QCUSTOM_" for a BJT.
 */
template <typename Model, typename Parameters>
class ModeledComponent : public Component {
public:
    // Ready model

    /**
     * @brief   Returns the ready model the part uses.
     * @return  An entry of the model list of its kind, or nullptr when it uses custom parameters or a library model.
     */
    const Model *GetModel() const { return HasLibraryModel() ? nullptr : m_Choice.GetModel(); }

    /**
     * @brief   Makes the part use a ready model.
     * @param[in] model  An entry of the model list of its kind; the part keeps a reference to it.
     */
    void SetModel(const Model &model) {
        m_Choice.SetModel(model);
        ClearLibraryModel();
    }

    // Custom parameters

    /**
     * @brief   Tells whether the part uses its own parameters instead of a ready model.
     * @return  True for a custom part; false for one with a ready or a library model.
     */
    bool IsCustom() const { return m_Choice.IsCustom() && !HasLibraryModel(); }

    /**
     * @brief   Switches the part to custom parameters, starting from those of its current model.
     * @note    Does nothing when it is already custom, so its parameters are kept.
     */
    void SetCustom() {
        ClearLibraryModel();
        m_Choice.SetCustom();
    }

    /**
     * @brief   Makes the part custom with the given parameters.
     * @param[in] parameters  New parameters; check them with the IsValidParameters() of the part first.
     */
    void SetCustomParameters(const Parameters &parameters) {
        ClearLibraryModel();
        m_Choice.SetCustomParameters(parameters);
    }

    // Simulation

    /**
     * @brief   Returns the parameters the part is simulated with.
     * @return  Those of its ready model, or its custom ones.
     */
    const Parameters &GetParameters() const { return m_Choice.GetParameters(); }

    /**
     * @brief   Returns the name of the model, as shown in the editor and saved in files.
     * @return  The ready model name, CUSTOM_MODEL_NAME, or the name of the library model it uses.
     */
    const char *GetModelName() const { return HasLibraryModel() ? GetLibraryModel().c_str() : m_Choice.GetName(); }

    /**
     * @brief   Returns the name of the ".model" line the part uses in a netlist.
     * @return  The ready model SPICE name, the library model one from GetLibraryModelSpiceName(), or the custom prefix
     *          of the part followed by its name, since custom parameters belong to one part.
     */
    std::string GetSpiceModelName() const {
        if (HasLibraryModel()) {
            return GetLibraryModelSpiceName(GetLibraryModel());
        }
        return m_Choice.IsCustom() ? std::format("{}{}", m_CustomPrefix, GetName())
                                   : std::string(m_Choice.GetModel()->SpiceName);
    }

protected:
    /**
     * @brief   Starts the part with a ready model.
     * @param[in] type           Kind of the part.
     * @param[in] initial        The first entry of the model list of its kind; the part keeps a reference to it.
     * @param[in] custom_prefix  Start of the ".model" name of a custom part, a literal that lives as long as the
     *                           program.
     */
    ModeledComponent(const ComponentType type, const Model &initial, const char *custom_prefix)
        : Component(type, 0.0), m_Choice(initial), m_CustomPrefix(custom_prefix) {}

private:
    ModelChoice<Model, Parameters> m_Choice;
    const char *m_CustomPrefix;
};

} // namespace Core

#endif // IMCSIM_MODELED_COMPONENT_H

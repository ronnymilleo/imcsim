/**
 * @file    model_choice.h
 * @brief   Which model a diode, transistor or op-amp uses: a ready one from its list, or custom parameters.
 */

#ifndef IMCSIM_MODEL_CHOICE_H
#define IMCSIM_MODEL_CHOICE_H

namespace Core {

/// Name every kind of part gives to its custom parameters in files and in the editor.
inline constexpr const char *CUSTOM_MODEL_NAME = "Custom";

/**
 * @class   ModelChoice
 * @brief   The model a part simulates with: an entry of its list of ready models, or its own parameters.
 * @details A part holds one as a member and forwards its model functions to it. Model must have the members Name and
 *          Parameters, of type Parameters. A model from an imported library is not kept here: it is a name that every
 *          component carries, and a part that takes one checks it before asking this class.
 */
template <typename Model, typename Parameters>
class ModelChoice {
public:
    /**
     * @brief   Starts with a ready model, whose parameters are also where the custom ones begin.
     * @param[in] initial  An entry of the list of the part; the choice keeps a reference to it.
     */
    explicit ModelChoice(const Model &initial) : m_Model(&initial), m_CustomParameters(initial.Parameters) {}

    // Ready model

    /**
     * @brief   Returns the ready model in use.
     * @return  An entry of the list of the part, or nullptr when the choice is custom.
     */
    const Model *GetModel() const { return m_Model; }

    /**
     * @brief   Switches to a ready model.
     * @param[in] model  An entry of the list of the part; the choice keeps a reference to it.
     */
    void SetModel(const Model &model) { m_Model = &model; }

    // Custom parameters

    /**
     * @brief   Tells whether the choice is custom parameters rather than a ready model.
     * @return  True when there is no ready model.
     */
    bool IsCustom() const { return m_Model == nullptr; }

    /**
     * @brief   Switches to custom parameters, starting from those of the current ready model.
     * @note    Does nothing when the choice is already custom, so its parameters are kept.
     */
    void SetCustom() {
        if (m_Model == nullptr) {
            return;
        }
        m_CustomParameters = m_Model->Parameters;
        m_Model = nullptr;
    }

    /**
     * @brief   Switches to custom parameters with the given values.
     * @param[in] parameters  New parameters; the part checks them before.
     */
    void SetCustomParameters(const Parameters &parameters) {
        m_CustomParameters = parameters;
        m_Model = nullptr;
    }

    // Reading

    /**
     * @brief   Returns the parameters to simulate with.
     * @return  Those of the ready model, or the custom ones.
     */
    const Parameters &GetParameters() const { return m_Model != nullptr ? m_Model->Parameters : m_CustomParameters; }

    /**
     * @brief   Returns the name of the model, as shown in the editor and saved in files.
     * @return  The name of the ready model, or CUSTOM_MODEL_NAME.
     */
    const char *GetName() const { return m_Model != nullptr ? m_Model->Name : CUSTOM_MODEL_NAME; }

private:
    const Model *m_Model; ///< Null when the choice is custom parameters.
    Parameters m_CustomParameters;
};

} // namespace Core

#endif // IMCSIM_MODEL_CHOICE_H

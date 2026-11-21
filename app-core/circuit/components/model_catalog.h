/**
 * @file    model_catalog.h
 * @brief   Looks up ready models in the list a family of parts keeps, by kind of part and by name.
 */

#ifndef IMCSIM_MODEL_CATALOG_H
#define IMCSIM_MODEL_CATALOG_H

#include "circuit/components/component.h"
#include <algorithm>
#include <span>
#include <string_view>

namespace Core {

/**
 * @brief   Returns the models of one kind of part in a list that groups them by kind.
 * @param[in] models  Every model of the family, with the models of each kind next to each other.
 * @param[in] type    Kind of component; Model::Type says which kind each model is for.
 * @return  The models of that kind, in list order; empty when the list has none.
 */
template <typename Model>
std::span<const Model> GetModelsOfType(const std::span<const Model> models, const ComponentType type) {
    const auto first = std::ranges::find(models, type, &Model::Type);
    const auto last = std::find_if(first, models.end(), [type](const Model &model) { return model.Type != type; });
    return {first, last};
}

/**
 * @brief   Finds a model by name.
 * @param[in] models  Models to search.
 * @param[in] name    Model name as in Model::Name, case sensitive.
 * @return  The model, or nullptr when none has that name.
 */
template <typename Model>
const Model *FindModelByName(const std::span<const Model> models, const std::string_view name) {
    const auto model = std::ranges::find(models, name, &Model::Name);
    return model != models.end() ? &*model : nullptr;
}

} // namespace Core

#endif // IMCSIM_MODEL_CATALOG_H

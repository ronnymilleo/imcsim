/**
 * @file    file_library_models.cpp
 * @brief   Writes the device models imported from libraries to JSON and reads them back.
 */

#include "schematic/file_library_models.h"

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <algorithm>
#include <format>
#include <optional>

namespace GUI {

/**
 * @brief   Writes the models as a JSON list.
 * @details A model is its text, from which its name and kind are read back.
 * @param[in] models  Models to write.
 * @return  The list, with one object holding the text of each model.
 */
nlohmann::json WriteLibraryModels(const std::span<const Core::LibraryModel> models) {
    nlohmann::json list = nlohmann::json::array();
    for (const Core::LibraryModel &model : models) {
        list.push_back({{"text", model.Text}});
    }
    return list;
}

/**
 * @brief   Reads the "models" list of a document.
 * @details A model that is not a diode or transistor .model, or repeats the name of one before it, is skipped with a
 *          warning.
 * @param[in]     document  Document object that holds the list.
 * @param[in,out] warnings  Receives one message for each model skipped.
 * @return  The models read, in the order of the file.
 */
std::vector<Core::LibraryModel> ReadLibraryModels(const nlohmann::json &document, std::vector<std::string> &warnings) {
    std::vector<Core::LibraryModel> models;
    const auto list = document.find("models");
    if (list == document.end()) {
        return models;
    }
    if (!list->is_array()) {
        warnings.emplace_back("The library models are not a list and were skipped");
        return models;
    }
    for (std::size_t index = 0; index < list->size(); ++index) {
        const nlohmann::json &entry = (*list)[index];
        const std::optional<std::string> text = entry.is_object() ? ReadString(entry, "text") : std::nullopt;
        std::optional<Core::LibraryModel> model = text ? Core::ReadLibraryModel(*text) : std::nullopt;
        if (!model) {
            warnings.push_back(
                std::format("Library model {} is not a diode or transistor .model and was skipped", index + 1));
        } else if (std::ranges::contains(models, model->Name, &Core::LibraryModel::Name)) {
            warnings.push_back(std::format("Library model {} is repeated and was skipped", model->Name));
        } else {
            models.push_back(std::move(*model));
        }
    }
    return models;
}

} // namespace GUI

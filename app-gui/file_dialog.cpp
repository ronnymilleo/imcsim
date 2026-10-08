/**
 * @file    file_dialog.cpp
 * @brief   Native open and save file dialogs whose results are picked up on the main thread.
 */

#include "file_dialog.h"

#include <utility>

namespace GUI {

/**
 * @brief   Tells whether a dialog is open and its answer not taken yet.
 * @return  True from ShowOpen() or ShowSave() until TakeResult() returns the answer.
 */
bool FileDialog::IsPending() const {
    return m_Pending;
}

/**
 * @brief   Shows the native dialog to pick an existing file, unless a dialog is already pending.
 * @param[in] filters   File types offered; must stay valid until the answer arrives, so pass a static array.
 * @param[in] location  File or folder the dialog starts at; empty for the default.
 */
void FileDialog::ShowOpen(const std::span<const SDL_DialogFileFilter> filters, std::string location) {
    if (std::exchange(m_Pending, true)) {
        return;
    }
    m_Location = std::move(location);
    // The callback owns this copy of the channel and deletes it, so the channel outlives the dialog's owner if needed
    auto *channel = new std::shared_ptr<Channel>(m_Channel);
    SDL_ShowOpenFileDialog(HandleResult, channel, nullptr, filters.data(), static_cast<int>(filters.size()),
                           m_Location.empty() ? nullptr : m_Location.c_str(), false);
}

/**
 * @brief   Shows the native dialog to pick where to save a file, unless a dialog is already pending.
 * @param[in] filters   File types offered; must stay valid until the answer arrives, so pass a static array.
 * @param[in] location  File or folder the dialog starts at; empty for the default.
 */
void FileDialog::ShowSave(const std::span<const SDL_DialogFileFilter> filters, std::string location) {
    if (std::exchange(m_Pending, true)) {
        return;
    }
    m_Location = std::move(location);
    auto *channel = new std::shared_ptr<Channel>(m_Channel);
    SDL_ShowSaveFileDialog(HandleResult, channel, nullptr, filters.data(), static_cast<int>(filters.size()),
                           m_Location.empty() ? nullptr : m_Location.c_str());
}

/**
 * @brief   Takes the answer of the pending dialog, if it has arrived.
 * @return  No value while the dialog is open or when none was shown; otherwise the chosen path, or no path when
 *          the user canceled or the dialog failed.
 */
std::optional<std::optional<std::filesystem::path>> FileDialog::TakeResult() {
    std::optional<std::optional<std::filesystem::path>> result;
    {
        const std::scoped_lock lock(m_Channel->Mutex);
        result = std::exchange(m_Channel->Result, std::nullopt);
    }
    if (result) {
        m_Pending = false;
    }
    return result;
}

// SDL may call this from another thread, and after the owner is gone, so it only stores the answer in the channel
// for the main thread to take. SDL calls it exactly once per dialog
void SDLCALL FileDialog::HandleResult(void *userdata, const char *const *file_list, int /*filter*/) {
    const std::unique_ptr<std::shared_ptr<Channel>> channel(static_cast<std::shared_ptr<Channel> *>(userdata));
    std::optional<std::filesystem::path> path;
    // A null list means an error and an empty list means the user canceled
    if (file_list != nullptr && file_list[0] != nullptr) {
        path = std::filesystem::path(file_list[0]);
    }
    const std::scoped_lock lock((*channel)->Mutex);
    (*channel)->Result = std::move(path);
}

} // namespace GUI

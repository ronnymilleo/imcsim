/**
 * @file    file_dialog.h
 * @brief   Native open and save file dialogs whose results are picked up on the main thread.
 */

#ifndef IMCSIM_FILE_DIALOG_H
#define IMCSIM_FILE_DIALOG_H

#include <SDL3/SDL_dialog.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace GUI {

/**
 * @class   FileDialog
 * @brief   One native file dialog at a time, shown through SDL and polled every frame for its answer.
 * @details SDL answers asynchronously, possibly from another thread and after the owner is gone, such as when the
 *          application quits with a dialog open. The answer goes to a channel shared with each pending dialog, so it
 *          always lands in memory that still exists, and the owner takes it on a later frame.
 */
class FileDialog {
public:
    bool IsPending() const;
    void ShowOpen(std::span<const SDL_DialogFileFilter> filters, std::string location);
    void ShowSave(std::span<const SDL_DialogFileFilter> filters, std::string location);
    std::optional<std::optional<std::filesystem::path>> TakeResult();

private:
    /**
     * @struct  Channel
     * @brief   Where the dialog callback leaves the chosen path, or no path when the dialog was canceled or failed.
     */
    struct Channel {
        // SDL may call the dialog callback from another thread
        std::mutex Mutex;
        std::optional<std::optional<std::filesystem::path>> Result;
    };

    bool m_Pending = false;
    std::string m_Location;
    std::shared_ptr<Channel> m_Channel = std::make_shared<Channel>();

    static void SDLCALL HandleResult(void *userdata, const char *const *file_list, int filter);
};

} // namespace GUI

#endif // IMCSIM_FILE_DIALOG_H

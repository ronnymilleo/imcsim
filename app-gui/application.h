/***********************************************************************************************************************
 Application GUI
***********************************************************************************************************************/

#ifndef IMCSIM_APPLICATION_H
#define IMCSIM_APPLICATION_H

#include "imgui_impl_vulkan.h"
#include "vulkan_context.h"
#include <SDL3/SDL_video.h>
#include <string>
#include <vector>

class Application {
  public:
    Application() = default;

    ~Application() = default;

    int Init();
    int Run();
    void Shutdown();

  private:
    float m_SDLWindowScale{};
    SDL_WindowFlags m_SDLWindowFlags{};
    SDL_Window *m_SDLWindow = nullptr;
    VulkanContext m_Vulkan;
    ImGui_ImplVulkan_InitInfo m_VulkanInitInfo{};
    std::vector<std::string> m_Errors;

    void AddError(const std::string &message);
    void DumpErrors();

    int InitSDL();
    int InitVulkan();
    int InitIMGUI();
};

#endif // IMCSIM_APPLICATION_H

/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/

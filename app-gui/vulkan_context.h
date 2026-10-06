/***********************************************************************************************************************
* @file    vulkan_context.h
 * @brief
 * @details
 *
 * @project imcsim
 * @author  ronnymilleo
 * @date    10/6/26
***********************************************************************************************************************/

#ifndef IMCSIM_VULKAN_SETUP_H
#define IMCSIM_VULKAN_SETUP_H

/***********************************************************************************************************************
 Includes
***********************************************************************************************************************/

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL_video.h>
#include <string>

void check_vk_result(VkResult err);

/***********************************************************************************************************************
 Class
***********************************************************************************************************************/

class VulkanContext {
  public:
    // Instance, physical/logical device, queue and descriptor pool.
    bool Init();
    // Surface, swap chain, render pass and framebuffers for the given window.
    bool InitWindow(SDL_Window *window);

    // Rebuilds the swap chain when the window size changed or a rebuild was requested.
    void ResizeIfNeeded(SDL_Window *window);
    void SetClearColor(const ImVec4 &color);
    void FrameRender(ImDrawData *draw_data);
    void FramePresent();

    void WaitIdle() const;
    // Safe to call after a partial Init(); destroys only what was created.
    void Shutdown();

    void FillImGuiInitInfo(ImGui_ImplVulkan_InitInfo &info);

    // Reason for the last Init()/InitWindow() failure.
    const std::string &LastError() const { return m_LastError; }

  private:
    VkAllocationCallbacks *m_Allocator = nullptr;
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    uint32_t m_QueueFamily = static_cast<uint32_t>(-1);
    VkQueue m_Queue = VK_NULL_HANDLE;
    VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT m_DebugReport = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

    ImGui_ImplVulkanH_Window m_WindowData;
    bool m_WindowCreated = false;
    uint32_t m_MinImageCount = 2;
    bool m_SwapChainRebuild = false;
    std::string m_LastError;

    void CreateInstance(ImVector<const char *> instance_extensions);
    void CreateDevice();
    void CreateDescriptorPool();
    void SetupWindow(int width, int height);
};

#endif // IMCSIM_VULKAN_SETUP_H

/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/

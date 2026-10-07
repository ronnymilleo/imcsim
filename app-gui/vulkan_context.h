/**
 * @file    vulkan_context.h
 * @brief   Vulkan instance, device, swap chain and frame submission used by the Dear ImGui renderer.
 */

#ifndef IMCSIM_VULKAN_CONTEXT_H
#define IMCSIM_VULKAN_CONTEXT_H

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <SDL3/SDL_video.h>
#include <string>

namespace GUI {

/**
 * @class   VulkanContext
 * @brief   Owns the Vulkan objects needed to render Dear ImGui into an SDL window.
 * @details Call Init() first, then InitWindow() with the target window. On failure, LastError() explains why.
 */
class VulkanContext {
public:
    // Setup and teardown
    int Init();
    int InitWindow(SDL_Window *window);
    void FillImGuiInitInfo(ImGui_ImplVulkan_InitInfo &info);
    void WaitIdle() const;
    void Shutdown();

    // Frame
    void ResizeIfNeeded(SDL_Window *window);
    void SetClearColor(const ImVec4 &color);
    void FrameRender(ImDrawData *draw_data);
    void FramePresent();

    // Errors
    const std::string &LastError() const;

private:
    // Vulkan objects
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

    // Window and swap chain
    ImGui_ImplVulkanH_Window m_WindowData;
    bool m_WindowCreated = false;
    uint32_t m_MinImageCount = 2;
    bool m_SwapChainRebuild = false;

    // Errors
    std::string m_LastError;

    // Setup steps
    void CreateInstance(ImVector<const char *> instance_extensions);
    void CreateDevice();
    void CreateDescriptorPool();
    int SetupWindow(int width, int height);
};

} // namespace GUI

#endif // IMCSIM_VULKAN_CONTEXT_H

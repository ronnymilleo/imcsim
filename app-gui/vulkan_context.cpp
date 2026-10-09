/**
 * @file    vulkan_context.cpp
 * @brief   Vulkan instance, device, swap chain and frame submission used by the Dear ImGui renderer.
 */

#include "vulkan_context.h"

#include "error_codes.h"
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// region Macro definitions

// Volk headers
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#define VOLK_IMPLEMENTATION
#include <volk.h>
#endif

// #define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif

// endregion

namespace GUI {

// region Private definitions

namespace {

void CheckVkResult(VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", result);
    if (result < 0) {
        abort();
    }
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
VKAPI_ATTR VkBool32 VKAPI_CALL DebugReport(VkDebugReportFlagsEXT, VkDebugReportObjectTypeEXT object_type, uint64_t,
                                           size_t, int32_t, const char *, const char *message, void *) {
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", object_type, message);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

bool IsExtensionAvailable(const ImVector<VkExtensionProperties> &properties, const char *extension) {
    for (const VkExtensionProperties &property : properties) {
        if (strcmp(property.extensionName, extension) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

// endregion

// region Definitions

/**
 * @brief   Creates the instance, selects the physical device and creates the logical device, queue and
 *          descriptor pool.
 * @return  Core::ExitSuccess on success, Core::ExitFailure otherwise.
 */
int VulkanContext::Init() {
    uint32_t sdl_extension_count = 0;
    const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (sdl_extensions == nullptr) {
        m_LastError = std::string("SDL_Vulkan_GetInstanceExtensions(): ") + SDL_GetError();
        return Core::ExitFailure;
    }

    ImVector<const char *> extensions;
    for (uint32_t n = 0; n < sdl_extension_count; n++) {
        extensions.push_back(sdl_extensions[n]);
    }

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkInitialize();
#endif

    CreateInstance(extensions);

    // Select physical device (GPU)
    m_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_Instance);
    IM_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE);

    // Select graphics queue family
    m_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_PhysicalDevice);
    IM_ASSERT(m_QueueFamily != static_cast<uint32_t>(-1));

    CreateDevice();
    CreateDescriptorPool();
    return Core::ExitSuccess;
}

/**
 * @brief   Creates the surface, swap chain, render pass and framebuffers for the given window.
 * @param[in] window  Window to render into. Must have been created with SDL_WINDOW_VULKAN.
 * @return  Core::ExitSuccess on success, Core::ExitFailure otherwise.
 */
int VulkanContext::InitWindow(SDL_Window *window) {
    if (!SDL_Vulkan_CreateSurface(window, m_Instance, m_Allocator, &m_Surface)) {
        m_LastError = std::string("SDL_Vulkan_CreateSurface(): ") + SDL_GetError();
        return Core::ExitFailure;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    return SetupWindow(width, height);
}

/**
 * @brief   Fills the Dear ImGui Vulkan backend init info with the handles owned by this context.
 * @param[out] info  Structure passed to ImGui_ImplVulkan_Init().
 */
void VulkanContext::FillImGuiInitInfo(ImGui_ImplVulkan_InitInfo &info) {
    // The instance is created without VkApplicationInfo, so it is Vulkan 1.0, the version ImGui must be told
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = m_Instance;
    info.PhysicalDevice = m_PhysicalDevice;
    info.Device = m_Device;
    info.QueueFamily = m_QueueFamily;
    info.Queue = m_Queue;
    info.PipelineCache = m_PipelineCache;
    info.DescriptorPool = m_DescriptorPool;
    info.MinImageCount = m_MinImageCount;
    info.ImageCount = m_WindowData.ImageCount;
    info.Allocator = m_Allocator;
    info.PipelineInfoMain.RenderPass = m_WindowData.RenderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = CheckVkResult;
}

/**
 * @brief   Blocks until the device has finished all submitted work.
 */
void VulkanContext::WaitIdle() const {
    if (m_Device != VK_NULL_HANDLE) {
        CheckVkResult(vkDeviceWaitIdle(m_Device));
    }
}

/**
 * @brief   Destroys every Vulkan object owned by this context.
 * @note    Safe to call after a partial Init(); destroys only what was created.
 */
void VulkanContext::Shutdown() {
    if (m_Device != VK_NULL_HANDLE) {
        if (m_WindowCreated) {
            ImGui_ImplVulkanH_DestroyWindow(m_Instance, m_Device, &m_WindowData, m_Allocator);
            m_WindowCreated = false;
        }
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, m_Allocator);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, m_Allocator);
        m_Surface = VK_NULL_HANDLE;

#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto destroy_debug_report_callback = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT"));
        destroy_debug_report_callback(m_Instance, m_DebugReport, m_Allocator);
        m_DebugReport = VK_NULL_HANDLE;
#endif
    }

    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, m_Allocator);
        m_Device = VK_NULL_HANDLE;
    }
    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, m_Allocator);
        m_Instance = VK_NULL_HANDLE;
    }
}

/**
 * @brief   Rebuilds the swap chain when the window size changed or a rebuild was requested.
 * @param[in] window  Window whose current size is checked.
 */
void VulkanContext::ResizeIfNeeded(SDL_Window *window) {
    int fb_width = 0;
    int fb_height = 0;
    SDL_GetWindowSizeInPixels(window, &fb_width, &fb_height);
    if (fb_width > 0 && fb_height > 0 &&
        (m_SwapChainRebuild || m_WindowData.Width != fb_width || m_WindowData.Height != fb_height)) {
        ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, &m_WindowData, m_QueueFamily,
                                               m_Allocator, fb_width, fb_height, m_MinImageCount, 0);
        m_WindowData.FrameIndex = 0;
        m_SwapChainRebuild = false;
    }
}

/**
 * @brief   Sets the color used to clear the framebuffer at the start of each frame.
 * @param[in] color  Straight-alpha RGBA color; it is premultiplied before use.
 */
void VulkanContext::SetClearColor(const ImVec4 &color) {
    m_WindowData.ClearValue.color.float32[0] = color.x * color.w;
    m_WindowData.ClearValue.color.float32[1] = color.y * color.w;
    m_WindowData.ClearValue.color.float32[2] = color.z * color.w;
    m_WindowData.ClearValue.color.float32[3] = color.w;
}

/**
 * @brief   Records and submits the command buffer for one frame of Dear ImGui draw data.
 * @param[in] draw_data  Draw data returned by ImGui::GetDrawData().
 */
void VulkanContext::FrameRender(ImDrawData *draw_data) {
    ImGui_ImplVulkanH_Window *window_data = &m_WindowData;
    VkSemaphore image_acquired_semaphore =
        window_data->FrameSemaphores[window_data->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore =
        window_data->FrameSemaphores[window_data->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult result = vkAcquireNextImageKHR(m_Device, window_data->Swapchain, UINT64_MAX, image_acquired_semaphore,
                                            VK_NULL_HANDLE, &window_data->FrameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_SwapChainRebuild = true;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        CheckVkResult(result);
    }

    ImGui_ImplVulkanH_Frame *frame = &window_data->Frames[window_data->FrameIndex];
    {
        // Wait indefinitely instead of periodically checking
        result = vkWaitForFences(m_Device, 1, &frame->Fence, VK_TRUE, UINT64_MAX);
        CheckVkResult(result);

        result = vkResetFences(m_Device, 1, &frame->Fence);
        CheckVkResult(result);
    }
    {
        result = vkResetCommandPool(m_Device, frame->CommandPool, 0);
        CheckVkResult(result);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(frame->CommandBuffer, &info);
        CheckVkResult(result);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = window_data->RenderPass;
        info.framebuffer = frame->Framebuffer;
        info.renderArea.extent.width = window_data->Width;
        info.renderArea.extent.height = window_data->Height;
        info.clearValueCount = 1;
        info.pClearValues = &window_data->ClearValue;
        vkCmdBeginRenderPass(frame->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record Dear ImGui primitives into the command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, frame->CommandBuffer);

    // Submit the command buffer
    vkCmdEndRenderPass(frame->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &frame->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        result = vkEndCommandBuffer(frame->CommandBuffer);
        CheckVkResult(result);
        result = vkQueueSubmit(m_Queue, 1, &info, frame->Fence);
        CheckVkResult(result);
    }
}

/**
 * @brief   Presents the last rendered frame to the window.
 */
void VulkanContext::FramePresent() {
    if (m_SwapChainRebuild) {
        return;
    }
    ImGui_ImplVulkanH_Window *window_data = &m_WindowData;
    VkSemaphore render_complete_semaphore =
        window_data->FrameSemaphores[window_data->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &window_data->Swapchain;
    info.pImageIndices = &window_data->FrameIndex;
    VkResult result = vkQueuePresentKHR(m_Queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_SwapChainRebuild = true;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        CheckVkResult(result);
    }
    window_data->SemaphoreIndex = (window_data->SemaphoreIndex + 1) % window_data->SemaphoreCount;
}

/**
 * @brief   Returns the reason for the last Init() or InitWindow() failure.
 */
const std::string &VulkanContext::LastError() const {
    return m_LastError;
}

void VulkanContext::CreateInstance(ImVector<const char *> instance_extensions) {
    VkResult result;
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    // Enumerate available extensions
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
    CheckVkResult(result);

    // Enable required extensions
    if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    // Enable validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
    instance_extensions.push_back("VK_EXT_debug_report");
#endif

    // Create the Vulkan instance
    create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.Size);
    create_info.ppEnabledExtensionNames = instance_extensions.Data;
    result = vkCreateInstance(&create_info, m_Allocator, &m_Instance);
    CheckVkResult(result);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkLoadInstance(m_Instance);
#endif

    // Set up the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto create_debug_report_callback = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
        vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT"));
    IM_ASSERT(create_debug_report_callback != nullptr);
    VkDebugReportCallbackCreateInfoEXT debug_report_info = {};
    debug_report_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_info.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_info.pfnCallback = DebugReport;
    debug_report_info.pUserData = nullptr;
    result = create_debug_report_callback(m_Instance, &debug_report_info, m_Allocator, &m_DebugReport);
    CheckVkResult(result);
#endif
}

void VulkanContext::CreateDevice() {
    ImVector<const char *> device_extensions;
    device_extensions.push_back("VK_KHR_swapchain");

    // Enumerate physical device extensions
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif

    const float queue_priority[] = {1.0f};
    VkDeviceQueueCreateInfo queue_info[1] = {};
    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = m_QueueFamily;
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = queue_priority;
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
    create_info.pQueueCreateInfos = queue_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.Size);
    create_info.ppEnabledExtensionNames = device_extensions.Data;
    const VkResult result = vkCreateDevice(m_PhysicalDevice, &create_info, m_Allocator, &m_Device);
    CheckVkResult(result);
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
}

void VulkanContext::CreateDescriptorPool() {
    // Loading additional textures may require larger pool sizes and maxSets
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
        {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize &pool_size : pool_sizes) {
        pool_info.maxSets += pool_size.descriptorCount;
    }
    pool_info.poolSizeCount = static_cast<uint32_t>(IM_COUNTOF(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;
    const VkResult result = vkCreateDescriptorPool(m_Device, &pool_info, m_Allocator, &m_DescriptorPool);
    CheckVkResult(result);
}

int VulkanContext::SetupWindow(int width, int height) {
    ImGui_ImplVulkanH_Window *window_data = &m_WindowData;

    // Check for WSI support
    VkBool32 surface_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, m_Surface, &surface_supported);
    if (surface_supported != VK_TRUE) {
        m_LastError = "The selected physical device cannot present to the window surface (no WSI support)";
        return Core::ExitFailure;
    }

    // Select surface format
    const VkFormat requested_formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM,
                                          VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR requested_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    window_data->Surface = m_Surface;
    window_data->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_PhysicalDevice, window_data->Surface, requested_formats, static_cast<size_t>(IM_COUNTOF(requested_formats)),
        requested_color_space);

    // Select present mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
                                        VK_PRESENT_MODE_FIFO_KHR};
#else
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
    window_data->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_PhysicalDevice, window_data->Surface,
                                                                   &present_modes[0], IM_COUNTOF(present_modes));

    // Create swap chain, render pass and framebuffers
    IM_ASSERT(m_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, window_data, m_QueueFamily,
                                           m_Allocator, width, height, m_MinImageCount, 0);
    m_WindowCreated = true;
    return Core::ExitSuccess;
}

// endregion

} // namespace GUI

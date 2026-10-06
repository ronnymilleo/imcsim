/***********************************************************************************************************************
 * @file    vulkan_context.cpp
 * @brief
 * @details
 *
 * @project imcsim
 * @author  ronnymilleo
 * @date    10/6/26
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes
***********************************************************************************************************************/

#include "vulkan_context.h"
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/***********************************************************************************************************************
 Macro definitions
***********************************************************************************************************************/

// Volk headers
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#define VOLK_IMPLEMENTATION
#include <volk.h>
#endif

// #define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif

/***********************************************************************************************************************
 Functions
***********************************************************************************************************************/

void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                                   uint64_t object, size_t location, int32_t messageCode,
                                                   const char *pLayerPrefix, const char *pMessage, void *pUserData) {
    (void)flags;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties> &properties, const char *extension) {
    for (const VkExtensionProperties &p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

/***********************************************************************************************************************
 Method Definitions
***********************************************************************************************************************/

bool VulkanContext::Init() {
    uint32_t sdl_extensions_count = 0;
    const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
    if (sdl_extensions == nullptr) {
        m_LastError = std::string("SDL_Vulkan_GetInstanceExtensions(): ") + SDL_GetError();
        return false;
    }

    ImVector<const char *> extensions;
    for (uint32_t n = 0; n < sdl_extensions_count; n++)
        extensions.push_back(sdl_extensions[n]);

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkInitialize();
#endif

    CreateInstance(extensions);

    // Select Physical Device (GPU)
    m_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_Instance);
    IM_ASSERT(m_PhysicalDevice != VK_NULL_HANDLE);

    // Select graphics queue family
    m_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_PhysicalDevice);
    IM_ASSERT(m_QueueFamily != static_cast<uint32_t>(-1));

    CreateDevice();
    CreateDescriptorPool();
    return true;
}

bool VulkanContext::InitWindow(SDL_Window *window) {
    if (!SDL_Vulkan_CreateSurface(window, m_Instance, m_Allocator, &m_Surface)) {
        m_LastError = std::string("SDL_Vulkan_CreateSurface(): ") + SDL_GetError();
        return false;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    SetupWindow(width, height);
    return true;
}

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

void VulkanContext::SetClearColor(const ImVec4 &color) {
    m_WindowData.ClearValue.color.float32[0] = color.x * color.w;
    m_WindowData.ClearValue.color.float32[1] = color.y * color.w;
    m_WindowData.ClearValue.color.float32[2] = color.z * color.w;
    m_WindowData.ClearValue.color.float32[3] = color.w;
}

void VulkanContext::FrameRender(ImDrawData *draw_data) {
    ImGui_ImplVulkanH_Window *wd = &m_WindowData;
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(m_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE,
                                         &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);

    ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE,
                              UINT64_MAX); // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(m_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(m_Device, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(m_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

void VulkanContext::FramePresent() {
    if (m_SwapChainRebuild)
        return;
    ImGui_ImplVulkanH_Window *wd = &m_WindowData;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(m_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

void VulkanContext::WaitIdle() const {
    if (m_Device != VK_NULL_HANDLE)
        check_vk_result(vkDeviceWaitIdle(m_Device));
}

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
        auto f_vkDestroyDebugReportCallbackEXT =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugReportCallbackEXT");
        f_vkDestroyDebugReportCallbackEXT(m_Instance, m_DebugReport, m_Allocator);
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

void VulkanContext::FillImGuiInitInfo(ImGui_ImplVulkan_InitInfo &info) {
    info.ApiVersion = VK_API_VERSION_1_4;
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
    info.CheckVkResultFn = check_vk_result;
}

void VulkanContext::CreateInstance(ImVector<const char *> instance_extensions) {
    VkResult err;
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    // Enumerate available extensions
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
    check_vk_result(err);

    // Enable required extensions
    if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
    instance_extensions.push_back("VK_EXT_debug_report");
#endif

    // Create Vulkan Instance
    create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.Size);
    create_info.ppEnabledExtensionNames = instance_extensions.Data;
    err = vkCreateInstance(&create_info, m_Allocator, &m_Instance);
    check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkLoadInstance(m_Instance);
#endif

    // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto f_vkCreateDebugReportCallbackEXT =
        (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugReportCallbackEXT");
    IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
    VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
    debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_ci.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_ci.pfnCallback = debug_report;
    debug_report_ci.pUserData = nullptr;
    err = f_vkCreateDebugReportCallbackEXT(m_Instance, &debug_report_ci, m_Allocator, &m_DebugReport);
    check_vk_result(err);
#endif
}

void VulkanContext::CreateDevice() {
    ImVector<const char *> device_extensions;
    device_extensions.push_back("VK_KHR_swapchain");

    // Enumerate physical device extension
    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
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
    const VkResult err = vkCreateDevice(m_PhysicalDevice, &create_info, m_Allocator, &m_Device);
    check_vk_result(err);
    vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
}

void VulkanContext::CreateDescriptorPool() {
    // If you wish to load e.g. additional textures you may need to alter pools
    // sizes and maxSets.
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
        {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize &pool_size : pool_sizes)
        pool_info.maxSets += pool_size.descriptorCount;
    pool_info.poolSizeCount = static_cast<uint32_t>(IM_COUNTOF(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;
    const VkResult err = vkCreateDescriptorPool(m_Device, &pool_info, m_Allocator, &m_DescriptorPool);
    check_vk_result(err);
}

void VulkanContext::SetupWindow(int width, int height) {
    ImGui_ImplVulkanH_Window *wd = &m_WindowData;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, m_Surface, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                  VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->Surface = m_Surface;
    wd->SurfaceFormat =
        ImGui_ImplVulkanH_SelectSurfaceFormat(m_PhysicalDevice, wd->Surface, requestSurfaceImageFormat,
                                              (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
                                        VK_PRESENT_MODE_FIFO_KHR};
#else
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_PhysicalDevice, wd->Surface, &present_modes[0],
                                                          IM_COUNTOF(present_modes));

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(m_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(m_Instance, m_PhysicalDevice, m_Device, wd, m_QueueFamily, m_Allocator,
                                           width, height, m_MinImageCount, 0);
    m_WindowCreated = true;
}

/***********************************************************************************************************************
 End of file
***********************************************************************************************************************/

/**
 * @file    glow_renderer.cpp
 * @brief   Draws soft glows that add light to what is under them, through an extra GPU pipeline beside Dear ImGui's.
 */

#include "overlays/glow_renderer.h"

#include "imgui_impl_sdlgpu3_shaders.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace GUI {

namespace {

/**
 * @struct  ShaderCode
 * @brief   The bytecode of the shaders of the ImGui backend for one GPU driver, and how to load it.
 */
struct ShaderCode {
    SDL_GPUShaderFormat Format = SDL_GPU_SHADERFORMAT_INVALID;
    const char *Entrypoint = "main";
    std::span<const std::uint8_t> Vertex;
    std::span<const std::uint8_t> Fragment;
};

/**
 * @struct  ShaderPair
 * @brief   The vertex and fragment shaders of a pipeline.
 */
struct ShaderPair {
    SDL_GPUShader *Vertex = nullptr;
    SDL_GPUShader *Fragment = nullptr;
};

/**
 * @brief   The shaders the ImGui backend builds its pipeline from, for the driver of a device.
 * @return  SPIR-V for Vulkan, DXBC for Direct3D 12 and, on Apple platforms, a Metal library or Metal source; empty code
 *          for another driver, which the shaders then fail to be created from.
 */
ShaderCode SelectShaders(SDL_GPUDevice *device) {
    const std::string_view driver = SDL_GetGPUDeviceDriver(device);
    if (driver == "vulkan") {
        return {.Format = SDL_GPU_SHADERFORMAT_SPIRV, .Vertex = spirv_vertex, .Fragment = spirv_fragment};
    }
    if (driver == "direct3d12") {
        return {.Format = SDL_GPU_SHADERFORMAT_DXBC, .Vertex = dxbc_vertex, .Fragment = dxbc_fragment};
    }
#ifdef __APPLE__
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_METALLIB) {
        return {.Format = SDL_GPU_SHADERFORMAT_METALLIB,
                .Entrypoint = "main0",
                .Vertex = metallib_vertex,
                .Fragment = metallib_fragment};
    }
    return {.Format = SDL_GPU_SHADERFORMAT_MSL, .Entrypoint = "main0", .Vertex = msl_vertex, .Fragment = msl_fragment};
#else
    return {};
#endif
}

/**
 * @brief   Creates the two shaders of the pipeline, with the uniforms and the sampler the backend binds.
 * @return  Both shaders, or none if either cannot be created, in which case the other is released.
 */
std::optional<ShaderPair> CreateShaders(SDL_GPUDevice *device, const ShaderCode &code) {
    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.entrypoint = code.Entrypoint;
    vertex_info.format = code.Format;
    vertex_info.code = code.Vertex.data();
    vertex_info.code_size = code.Vertex.size();
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.entrypoint = code.Entrypoint;
    fragment_info.format = code.Format;
    fragment_info.code = code.Fragment.data();
    fragment_info.code_size = code.Fragment.size();
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;

    const ShaderPair shaders = {.Vertex = SDL_CreateGPUShader(device, &vertex_info),
                                .Fragment = SDL_CreateGPUShader(device, &fragment_info)};
    if (shaders.Vertex != nullptr && shaders.Fragment != nullptr) {
        return shaders;
    }
    // Releasing a null shader would replace the error SDL_GetError() reports
    for (SDL_GPUShader *shader : {shaders.Vertex, shaders.Fragment}) {
        if (shader != nullptr) {
            SDL_ReleaseGPUShader(device, shader);
        }
    }
    return std::nullopt;
}

/**
 * @brief   The color target of the pipeline: the color adds its light weighted by alpha, and the alpha of the target is
 *          left as it is, since the swapchain should stay opaque.
 */
SDL_GPUColorTargetDescription MakeAdditiveTarget(const SDL_GPUTextureFormat target_format) {
    SDL_GPUColorTargetDescription target{};
    target.format = target_format;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    return target;
}

} // namespace

/**
 * @brief   Creates the additive pipeline and the falloff texture.
 * @param[in] device         GPU device the ImGui backend renders with.
 * @param[in] target_format  Format of the texture the frame is drawn to, the one the ImGui backend was given.
 * @return  EXIT_SUCCESS on success, EXIT_FAILURE if a GPU object cannot be created; SDL_GetError() tells
 *          why. Shutdown() releases what was created either way.
 * @note    Call it after the ImGui backend is initialized; the pipeline matches its sample count of one.
 */
int GlowRenderer::Init(SDL_GPUDevice *device, const SDL_GPUTextureFormat target_format) {
    m_Device = device;
    if (CreatePipeline(target_format) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    return CreateTexture();
}

/**
 * @brief   Releases the pipeline and the texture.
 * @note    The GPU must be idle, as the application makes it before it shuts down.
 */
void GlowRenderer::Shutdown() {
    if (m_Device == nullptr) {
        return;
    }
    if (m_Pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_Device, m_Pipeline);
        m_Pipeline = nullptr;
    }
    if (m_Texture != nullptr) {
        SDL_ReleaseGPUTexture(m_Device, m_Texture);
        m_Texture = nullptr;
    }
    m_Device = nullptr;
}

/**
 * @brief   Hands over the render pass the draw data is rendered in, for the callbacks to bind the pipeline in.
 * @param[in] render_pass  The current render pass, or nullptr once it ends.
 */
void GlowRenderer::SetRenderPass(SDL_GPURenderPass *render_pass) {
    m_RenderPass = render_pass;
}

/**
 * @brief   Adds glows to a draw list.
 * @param[in,out] draw_list  Where the glows go, with its current clipping.
 * @param[in] glows          Glows to draw, in screen pixels.
 * @param[in] additive       True to add their light to what is below, for dark backgrounds; false to blend them by
 *                           alpha like the rest of ImGui, for light backgrounds, which additive light would wash out
 *                           to white.
 * @note    Draws nothing if Init() failed to create the texture, and blends by alpha without the pipeline.
 */
void GlowRenderer::AddGlows(ImDrawList *draw_list, const std::span<const Glow> glows, const bool additive) {
    if (m_Texture == nullptr || glows.empty()) {
        return;
    }
    const bool switch_pipeline = additive && m_Pipeline != nullptr;
    if (switch_pipeline) {
        draw_list->AddCallback(&GlowRenderer::BindPipeline, this);
    }
    const auto texture = static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(m_Texture));
    for (const Glow &glow : glows) {
        const ImVec2 extent = {glow.Radius, glow.Radius};
        draw_list
            ->AddImage(texture, glow.Center - extent, glow.Center + extent, {0.0f, 0.0f}, {1.0f, 1.0f}, glow.Color);
    }
    if (switch_pipeline) {
        // The backend binds its own pipeline, buffers and viewport again
        draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    }
}

/**
 * @brief   Same vertex layout, shaders and state as the pipeline of the ImGui backend, except for the blending, so the
 *          glows can be drawn from the vertex and index buffers the backend has already bound.
 */
int GlowRenderer::CreatePipeline(const SDL_GPUTextureFormat target_format) {
    const std::optional<ShaderPair> shaders = CreateShaders(m_Device, SelectShaders(m_Device));
    if (!shaders) {
        return EXIT_FAILURE;
    }

    SDL_GPUVertexBufferDescription buffer_description{};
    buffer_description.slot = 0;
    buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffer_description.pitch = sizeof(ImDrawVert);
    const std::array<SDL_GPUVertexAttribute, 3> attributes = {{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ImDrawVert, pos)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ImDrawVert, uv)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(ImDrawVert, col)},
    }};
    const SDL_GPUColorTargetDescription target = MakeAdditiveTarget(target_format);

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = shaders->Vertex;
    pipeline_info.fragment_shader = shaders->Fragment;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = &buffer_description;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes.data();
    pipeline_info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.target_info.color_target_descriptions = &target;
    pipeline_info.target_info.num_color_targets = 1;
    m_Pipeline = SDL_CreateGPUGraphicsPipeline(m_Device, &pipeline_info);

    // The pipeline keeps what it needs from the shaders
    SDL_ReleaseGPUShader(m_Device, shaders->Vertex);
    SDL_ReleaseGPUShader(m_Device, shaders->Fragment);
    return m_Pipeline != nullptr ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * @brief   A texture of white texels whose alpha falls off from the center, so the vertex color of each glow tints it.
 */
int GlowRenderer::CreateTexture() {
    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = GLOW_TEXTURE_SIZE;
    texture_info.height = GLOW_TEXTURE_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_Texture = SDL_CreateGPUTexture(m_Device, &texture_info);
    if (m_Texture == nullptr) {
        return EXIT_FAILURE;
    }
    return UploadTexture(BuildFalloffTexels());
}

/**
 * @brief   Copies the texels into the texture through a transfer buffer and a copy pass.
 * @param[in] texels  The whole texture, row by row.
 * @return  EXIT_SUCCESS once the copy is submitted, EXIT_FAILURE if a GPU object cannot be had.
 */
int GlowRenderer::UploadTexture(const GlowTexels &texels) {
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(texels.size());
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(m_Device, &transfer_info);
    if (transfer_buffer == nullptr) {
        return EXIT_FAILURE;
    }
    void *mapped = SDL_MapGPUTransferBuffer(m_Device, transfer_buffer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
        return EXIT_FAILURE;
    }
    std::memcpy(mapped, texels.data(), texels.size());
    SDL_UnmapGPUTransferBuffer(m_Device, transfer_buffer);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(m_Device);
    if (command_buffer == nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
        return EXIT_FAILURE;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer_buffer;
    SDL_GPUTextureRegion destination{};
    destination.texture = m_Texture;
    destination.w = GLOW_TEXTURE_SIZE;
    destination.h = GLOW_TEXTURE_SIZE;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    const bool submitted = SDL_SubmitGPUCommandBuffer(command_buffer);
    // A released transfer buffer lives until the copy that uses it is done
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
    return submitted ? EXIT_SUCCESS : EXIT_FAILURE;
}

/**
 * @brief   Draw callback that switches to the additive pipeline.
 * @details The backend keeps its buffers, viewport and uniforms bound across the switch, and binds the texture and
 *          scissor of each following draw command as usual.
 */
void GlowRenderer::BindPipeline(const ImDrawList * /*draw_list*/, const ImDrawCmd *command) {
    const auto *renderer = static_cast<const GlowRenderer *>(command->UserCallbackData);
    if (renderer->m_RenderPass != nullptr) {
        SDL_BindGPUGraphicsPipeline(renderer->m_RenderPass, renderer->m_Pipeline);
    }
}

} // namespace GUI

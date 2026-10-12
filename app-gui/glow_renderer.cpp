/**
 * @file    glow_renderer.cpp
 * @brief   Draws soft glows that add light to what is under them, through an extra GPU pipeline beside Dear ImGui's.
 */

#include "glow_renderer.h"

#include "error_codes.h"
#include "imgui_impl_sdlgpu3_shaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace GUI {

namespace {

// Side of the falloff texture, in texels; the glow is smooth, so a small texture scales up without steps
constexpr std::uint32_t TextureSize = 64;
// The falloff is a broad halo plus a narrow core, so a glow looks hot in the middle. The core width is a fraction of
// the radius and the weights add up to 1, the alpha at the center
constexpr float HaloWeight = 0.65f;
constexpr float CoreWeight = 0.35f;
constexpr float CoreWidth = 0.2f;

// Alpha of the glow at a distance from its center, as a fraction of its radius
float Falloff(const float distance) {
    if (distance >= 1.0f) {
        return 0.0f;
    }
    const float halo = (1.0f - distance) * (1.0f - distance);
    const float core = std::exp(-(distance / CoreWidth) * (distance / CoreWidth));
    return HaloWeight * halo + CoreWeight * core;
}

} // namespace

/**
 * @brief   Creates the additive pipeline and the falloff texture.
 * @param[in] device         GPU device the ImGui backend renders with.
 * @param[in] target_format  Format of the texture the frame is drawn to, the one the ImGui backend was given.
 * @return  Core::ExitSuccess on success, Core::ExitFailure if a GPU object cannot be created; SDL_GetError() tells
 *          why. Shutdown() releases what was created either way.
 * @note    Call it after the ImGui backend is initialized; the pipeline matches its sample count of one.
 */
int GlowRenderer::Init(SDL_GPUDevice *device, const SDL_GPUTextureFormat target_format) {
    m_Device = device;
    if (CreatePipeline(target_format) != Core::ExitSuccess) {
        return Core::ExitFailure;
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
        draw_list->AddImage(texture, glow.Center - extent, glow.Center + extent, {0.0f, 0.0f}, {1.0f, 1.0f},
                            glow.Color);
    }
    if (switch_pipeline) {
        // The backend binds its own pipeline, buffers and viewport again
        draw_list->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    }
}

// Same vertex layout, shaders and state as the pipeline of the ImGui backend, except for the blending, so the
// glows can be drawn from the vertex and index buffers the backend has already bound
int GlowRenderer::CreatePipeline(const SDL_GPUTextureFormat target_format) {
    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.entrypoint = "main";
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.num_uniform_buffers = 1;
    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.entrypoint = "main";
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;

    const std::string_view driver = SDL_GetGPUDeviceDriver(m_Device);
    if (driver == "vulkan") {
        vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vertex_info.code = spirv_vertex;
        vertex_info.code_size = sizeof(spirv_vertex);
        fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fragment_info.code = spirv_fragment;
        fragment_info.code_size = sizeof(spirv_fragment);
    } else if (driver == "direct3d12") {
        vertex_info.format = SDL_GPU_SHADERFORMAT_DXBC;
        vertex_info.code = dxbc_vertex;
        vertex_info.code_size = sizeof(dxbc_vertex);
        fragment_info.format = SDL_GPU_SHADERFORMAT_DXBC;
        fragment_info.code = dxbc_fragment;
        fragment_info.code_size = sizeof(dxbc_fragment);
    }
#ifdef __APPLE__
    else if (SDL_GetGPUShaderFormats(m_Device) & SDL_GPU_SHADERFORMAT_METALLIB) {
        vertex_info.entrypoint = "main0";
        vertex_info.format = SDL_GPU_SHADERFORMAT_METALLIB;
        vertex_info.code = metallib_vertex;
        vertex_info.code_size = sizeof(metallib_vertex);
        fragment_info.entrypoint = "main0";
        fragment_info.format = SDL_GPU_SHADERFORMAT_METALLIB;
        fragment_info.code = metallib_fragment;
        fragment_info.code_size = sizeof(metallib_fragment);
    } else {
        vertex_info.entrypoint = "main0";
        vertex_info.format = SDL_GPU_SHADERFORMAT_MSL;
        vertex_info.code = msl_vertex;
        vertex_info.code_size = sizeof(msl_vertex);
        fragment_info.entrypoint = "main0";
        fragment_info.format = SDL_GPU_SHADERFORMAT_MSL;
        fragment_info.code = msl_fragment;
        fragment_info.code_size = sizeof(msl_fragment);
    }
#endif

    SDL_GPUShader *vertex_shader = SDL_CreateGPUShader(m_Device, &vertex_info);
    SDL_GPUShader *fragment_shader = SDL_CreateGPUShader(m_Device, &fragment_info);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        // Releasing a null shader would replace the error SDL_GetError() reports
        for (SDL_GPUShader *shader : {vertex_shader, fragment_shader}) {
            if (shader != nullptr) {
                SDL_ReleaseGPUShader(m_Device, shader);
            }
        }
        return Core::ExitFailure;
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

    // The color adds its light weighted by alpha; the alpha of the target is left as it is, since the swapchain
    // should stay opaque
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

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
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
    SDL_ReleaseGPUShader(m_Device, vertex_shader);
    SDL_ReleaseGPUShader(m_Device, fragment_shader);
    return m_Pipeline != nullptr ? Core::ExitSuccess : Core::ExitFailure;
}

// White texels whose alpha falls off from the center, so the vertex color of each glow tints it
int GlowRenderer::CreateTexture() {
    std::array<std::uint8_t, TextureSize * TextureSize * 4> texels{};
    for (std::uint32_t row = 0; row < TextureSize; ++row) {
        for (std::uint32_t column = 0; column < TextureSize; ++column) {
            // Texel centers, from -1 to 1 across the texture
            const float x = (static_cast<float>(column) + 0.5f) / TextureSize * 2.0f - 1.0f;
            const float y = (static_cast<float>(row) + 0.5f) / TextureSize * 2.0f - 1.0f;
            const float alpha = std::clamp(Falloff(std::sqrt(x * x + y * y)), 0.0f, 1.0f);
            std::uint8_t *texel = &texels[(row * TextureSize + column) * 4];
            texel[0] = texel[1] = texel[2] = 255;
            texel[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    }

    SDL_GPUTextureCreateInfo texture_info{};
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = TextureSize;
    texture_info.height = TextureSize;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_Texture = SDL_CreateGPUTexture(m_Device, &texture_info);
    if (m_Texture == nullptr) {
        return Core::ExitFailure;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(texels.size());
    SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(m_Device, &transfer_info);
    if (transfer_buffer == nullptr) {
        return Core::ExitFailure;
    }
    void *mapped = SDL_MapGPUTransferBuffer(m_Device, transfer_buffer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
        return Core::ExitFailure;
    }
    std::memcpy(mapped, texels.data(), texels.size());
    SDL_UnmapGPUTransferBuffer(m_Device, transfer_buffer);

    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(m_Device);
    if (command_buffer == nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
        return Core::ExitFailure;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer_buffer;
    SDL_GPUTextureRegion destination{};
    destination.texture = m_Texture;
    destination.w = TextureSize;
    destination.h = TextureSize;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    const bool submitted = SDL_SubmitGPUCommandBuffer(command_buffer);
    // A released transfer buffer lives until the copy that uses it is done
    SDL_ReleaseGPUTransferBuffer(m_Device, transfer_buffer);
    return submitted ? Core::ExitSuccess : Core::ExitFailure;
}

// Draw callback that switches to the additive pipeline. The backend keeps its buffers, viewport and uniforms bound
// across the switch, and binds the texture and scissor of each following draw command as usual
void GlowRenderer::BindPipeline(const ImDrawList * /*draw_list*/, const ImDrawCmd *command) {
    const auto *renderer = static_cast<const GlowRenderer *>(command->UserCallbackData);
    if (renderer->m_RenderPass != nullptr) {
        SDL_BindGPUGraphicsPipeline(renderer->m_RenderPass, renderer->m_Pipeline);
    }
}

} // namespace GUI

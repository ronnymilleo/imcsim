/**
 * @file    glow_renderer.h
 * @brief   Draws soft glows that add light to what is under them, through an extra GPU pipeline beside Dear ImGui's.
 */

#ifndef IMCSIM_GLOW_RENDERER_H
#define IMCSIM_GLOW_RENDERER_H

#include "imgui.h"
#include <SDL3/SDL_gpu.h>
#include <span>

namespace GUI {

/**
 * @struct  Glow
 * @brief   A round glow on screen: its center and radius in pixels, and its color, whose alpha is its intensity.
 */
struct Glow {
    ImVec2 Center;
    float Radius = 0.0f;
    ImU32 Color = 0;
};

/**
 * @class   GlowRenderer
 * @brief   Adds glows to an ImGui draw list with additive blending, so they light up the canvas like LEDs.
 * @details Dear ImGui blends by alpha, which can only cover the background, never brighten it. This class keeps a
 *          second pipeline built from the shaders of the ImGui backend, with additive blending, and a texture with a
 *          radial falloff. The glows are textured quads on the draw list, so they keep its clipping and order;
 *          callbacks around them switch to the additive pipeline and back. The callbacks need the render pass, which
 *          the application hands over while it renders the draw data.
 */
class GlowRenderer {
public:
    GlowRenderer() = default;
    ~GlowRenderer() = default;
    GlowRenderer(const GlowRenderer &) = delete;
    GlowRenderer &operator=(const GlowRenderer &) = delete;

    int Init(SDL_GPUDevice *device, SDL_GPUTextureFormat target_format);
    void Shutdown();
    void SetRenderPass(SDL_GPURenderPass *render_pass);

    void AddGlows(ImDrawList *draw_list, std::span<const Glow> glows, bool additive);

private:
    SDL_GPUDevice *m_Device = nullptr;
    SDL_GPUGraphicsPipeline *m_Pipeline = nullptr;
    SDL_GPUTexture *m_Texture = nullptr;
    // Only set while the application renders the draw data, the only time the callbacks run
    SDL_GPURenderPass *m_RenderPass = nullptr;

    int CreatePipeline(SDL_GPUTextureFormat target_format);
    int CreateTexture();
    static void BindPipeline(const ImDrawList *draw_list, const ImDrawCmd *command);
};

} // namespace GUI

#endif // IMCSIM_GLOW_RENDERER_H

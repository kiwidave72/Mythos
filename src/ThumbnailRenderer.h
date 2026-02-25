#pragma once
#include <glad/glad.h>
#include "MeshAsset.h"
#include "AssetLibrary.h"
#include <glm/glm.hpp>

// Renders a mesh asset into a small offscreen framebuffer and
// returns the GL texture ID for use with ImGui::Image.
//
// One ThumbnailRenderer is shared across all assets.
// Call renderThumbnail() when thumbDirty is true to refresh.
class ThumbnailRenderer
{
public:
    static constexpr int SIZE = 128;   // thumbnail pixel dimensions

    bool init();
    void shutdown();

    // Render asset's mesh into its thumbnail texture.
    // Updates entry.thumbnailTex and clears entry.thumbDirty.
    void renderThumbnail(AssetEntry& entry);

private:
    GLuint m_fbo     = 0;
    GLuint m_rbo     = 0;   // depth renderbuffer
    GLuint m_shader  = 0;

    // Shared scratch texture — we blit into entry.thumbnailTex after rendering
    GLuint m_colorTex = 0;

    void ensureTexture(GLuint& tex);
    void setUniforms(const GpuMesh& mesh, const AssetEntry& entry);
};

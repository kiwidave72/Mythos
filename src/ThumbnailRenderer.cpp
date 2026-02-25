#include "ThumbnailRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

// ---- Embedded shaders ------------------------------------------------------

static const char* THUMB_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vPos;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vPos = vec3(uModel * vec4(aPos, 1.0));
}
)GLSL";

static const char* THUMB_FRAG = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vPos;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(1.5, 2.0, 1.0));
    float diff = max(dot(N, L), 0.0);
    vec3 col = uColor * (0.25 + 0.75 * diff);
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ---- Shader helper ---------------------------------------------------------

static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "[ThumbnailRenderer] Shader error: " << log << "\n";
    }
    return s;
}

static GLuint buildProgram(const char* vert, const char* frag)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    GLuint p  = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ============================================================
// Init / shutdown
// ============================================================

bool ThumbnailRenderer::init()
{
    m_shader = buildProgram(THUMB_VERT, THUMB_FRAG);

    // FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Colour attachment (scratch — will be replaced per asset)
    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SIZE, SIZE, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_colorTex, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &m_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SIZE, SIZE);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, m_rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ThumbnailRenderer] FBO incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[ThumbnailRenderer] Ready (" << SIZE << "x" << SIZE << ")\n";
    return true;
}

void ThumbnailRenderer::shutdown()
{
    if (m_fbo)      { glDeleteFramebuffers(1,  &m_fbo);      m_fbo      = 0; }
    if (m_rbo)      { glDeleteRenderbuffers(1, &m_rbo);      m_rbo      = 0; }
    if (m_colorTex) { glDeleteTextures(1,      &m_colorTex); m_colorTex = 0; }
    if (m_shader)   { glDeleteProgram(m_shader);              m_shader   = 0; }
}

// ============================================================
// ensureTexture — allocate or reuse the per-asset texture
// ============================================================

void ThumbnailRenderer::ensureTexture(GLuint& tex)
{
    if (tex) return;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SIZE, SIZE, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// ============================================================
// renderThumbnail
// ============================================================

void ThumbnailRenderer::renderThumbnail(AssetEntry& entry)
{
    if (!entry.mesh || !entry.mesh->isLoaded()) return;

    ensureTexture(entry.thumbnailTex);

    // Save GL state we're about to clobber
    GLint prevFBO, prevViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Bind our FBO, attach the per-asset texture as colour target
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, entry.thumbnailTex, 0);

    glViewport(0, 0, SIZE, SIZE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.15f, 0.16f, 0.20f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const GpuMesh& gpu = entry.mesh->gpu;
    const MeshData& d  = entry.mesh->data;

    // Camera: fit the mesh in view using its AABB
    glm::vec3 centre = d.centre();
    glm::vec3 size   = d.size();

    // Use the longest axis as radius — guarantees non-zero even for flat tiles
    float radius = std::max({size.x, size.y, size.z}) * 0.6f;
    if (radius < 0.01f) radius = 1.f;

    // Apply calibration so the thumbnail matches what you'll see in-scene
    glm::mat4 calib = entry.calibMatrix();
    glm::vec4 cc    = calib * glm::vec4(centre, 1.f);
    centre = glm::vec3(cc);

    // Orbit camera — fixed 45°/30°
    float dist = radius * 2.5f;
    float yaw  = glm::radians(45.f);
    float pit  = glm::radians(35.f);
    glm::vec3 camPos = centre + dist * glm::vec3(
        cos(pit) * sin(yaw),
        sin(pit),
        cos(pit) * cos(yaw));

    // Safe up vector — if viewing almost straight down, use Z instead
    glm::vec3 up = {0,1,0};
    glm::vec3 fwd = glm::normalize(centre - camPos);
    if (std::abs(glm::dot(fwd, up)) > 0.99f) up = {0,0,1};

    // Robust near/far — never allow near < 0.001
    float nearZ = std::max(radius * 0.01f, 0.001f);
    float farZ  = radius * 15.f;

    glm::mat4 view = glm::lookAt(camPos, centre, up);
    glm::mat4 proj = glm::perspective(glm::radians(40.f), 1.f, nearZ, farZ);
    glm::mat4 mvp  = proj * view * calib;

    glUseProgram(m_shader);

    // Sanity check — NaN matrix would crash the AMD driver
    if (std::isnan(mvp[0][0]) || std::isinf(mvp[0][0])) {
        std::cerr << "[ThumbnailRenderer] Degenerate MVP for '" << entry.name << "' — skipping\n";
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        entry.thumbDirty = false;
        return;
    }

    glUniformMatrix4fv(glGetUniformLocation(m_shader, "uMVP"),   1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(m_shader, "uModel"), 1, GL_FALSE, glm::value_ptr(calib));

    glBindVertexArray(gpu.vao);

    if (!entry.mesh->submeshes.empty()) {
        // Multi-material mesh — draw each submesh with its own colour
        for (const SubMesh& sm : entry.mesh->submeshes) {
            glUniform3fv(glGetUniformLocation(m_shader, "uColor"), 1,
                         glm::value_ptr(sm.color));
            glDrawElements(GL_TRIANGLES, sm.indexCount, GL_UNSIGNED_INT,
                           (void*)(uintptr_t)sm.indexOffset);
        }
    } else {
        // Single-colour fallback
        glUniform3f(glGetUniformLocation(m_shader, "uColor"), 0.75f, 0.78f, 0.85f);
        glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glUseProgram(0);

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);

    entry.thumbDirty = false;
}

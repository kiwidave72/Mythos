#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include "MeshAsset.h"

// Forward declarations
struct SceneObject;

// ---- Camera ----------------------------------------------------------------
struct Camera
{
    glm::vec3 target  = {0.f, 0.f, 0.f};
    float     yaw     = -45.f;
    float     pitch   =  30.f;
    float     dist    =  10.f;

    glm::vec3 position() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;

    void orbit(float dYaw, float dPitch);
    void pan(float dx, float dy);
    void zoom(float delta);

    std::pair<glm::vec3,glm::vec3> screenRay(
        float screenX, float screenY,
        int viewportW, int viewportH) const;
};

// ---- Renderer --------------------------------------------------------------
class Renderer
{
public:
    bool init();
    void shutdown();

    void beginFrame(int viewportW, int viewportH);
    void endFrame();

    void drawGrid(const Camera& cam, int viewportW, int viewportH);

    // Draw a SceneObject using its mesh asset and transform.
    // Highlights selected/hovered objects automatically.
    void drawSceneObject(const Camera& cam,
                         const SceneObject& obj,
                         int viewportW, int viewportH);

    // Low-level: draw any GpuMesh with an explicit transform + color
    void drawMesh(const Camera& cam,
                  const GpuMesh& mesh,
                  const glm::mat4& model,
                  const glm::vec3& color,
                  int viewportW, int viewportH);

    // Ghost wireframe cube for 3D cursor
    void drawGhostCube(const Camera& cam,
                       const glm::mat4& model,
                       const glm::vec3& color,
                       float alpha,
                       int viewportW, int viewportH);

    // Legacy solid cube (used internally, kept for compatibility)
    void drawCube(const Camera& cam,
                  const glm::mat4& model,
                  const glm::vec3& color,
                  int viewportW, int viewportH);

private:
    GLuint  m_meshShader  = 0;
    GpuMesh m_cubeMesh;         // built-in unit cube (for legacy + ghost fill)

    GLuint  m_ghostShader = 0;
    GpuMesh m_wireEdges;

    GLuint m_gridShader    = 0;
    GLuint m_gridVAO       = 0;
    GLuint m_gridVBO       = 0;
    int    m_gridLineCount = 0;

    void buildCubeMesh();
    void buildWireEdges();
    void buildGrid();

    void setMeshUniforms(GLuint shader, const Camera& cam,
                         const glm::mat4& model, const glm::vec3& color,
                         int vpW, int vpH);
};


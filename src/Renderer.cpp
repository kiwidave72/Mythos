#include "Renderer.h"
#include "SceneObject.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// ============================================================
// Shaders embedded as string literals — no file path needed
// ============================================================

static const char* MESH_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
out vec3 vFragPos;
void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos      = worldPos.xyz;
    vNormal       = uNormalMatrix * aNormal;
    gl_Position   = uProjection * uView * worldPos;
}
)GLSL";

static const char* MESH_FRAG = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vFragPos;
uniform vec3 uColor;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
out vec4 FragColor;
void main()
{
    vec3 norm     = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    float ambient = 0.25;
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3  viewDir = normalize(uViewPos - vFragPos);
    vec3  halfDir = normalize(lightDir + viewDir);
    float spec    = pow(max(dot(norm, halfDir), 0.0), 32.0) * 0.4;
    vec3 result = (ambient + diff + spec) * uColor;
    FragColor   = vec4(result, 1.0);
}
)GLSL";

// ---- UE5-style infinite procedural grid ----
// Renders a giant flat quad; the fragment shader reconstructs world XZ position
// and draws multi-level grid lines with distance fade, axis colouring, and
// adaptive LOD that matches the camera zoom level.

static const char* GRID_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;

out vec3 vNear;
out vec3 vFar;

uniform mat4 uInvVP;

vec3 unproject(vec2 xy, float z)
{
    vec4 h = uInvVP * vec4(xy, z, 1.0);
    return h.xyz / h.w;
}

void main()
{
    vNear = unproject(aPos, -1.0);
    vFar  = unproject(aPos,  1.0);
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* GRID_FRAG = R"GLSL(
#version 330 core
in vec3 vNear;
in vec3 vFar;

out vec4 FragColor;

uniform float uCamDist;
uniform vec3  uCamPos;
uniform mat4  uVP;      // proj * view — needed to compute true fragment depth

// ---- noise -----------------------------------------------------------------

// Classic hash — maps a 2D cell coordinate to a pseudo-random float [0,1]
float hash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth value noise: bilinear interpolation between hashed cell corners
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);   // smoothstep curve
    float a = hash(i);
    float b = hash(i + vec2(1,0));
    float c = hash(i + vec2(0,1));
    float d = hash(i + vec2(1,1));
    return mix(mix(a,b,u.x), mix(c,d,u.x), u.y);
}

// 3 octaves of value noise — more detail, lower amplitude each octave
float fbm(vec2 p) {
    float v = 0.0;
    v += 0.500 * valueNoise(p * 1.0);
    v += 0.250 * valueNoise(p * 2.1);
    v += 0.125 * valueNoise(p * 4.3);
    return v;   // range ~[0, 0.875], remapped below
}

// ---- grid helpers ----------------------------------------------------------

float gridLine(vec2 p, float cellSize) {
    vec2 wrapped = abs(fract(p / cellSize + 0.5) - 0.5) * cellSize;
    vec2 fw      = fwidth(p);
    vec2 cover   = smoothstep(fw, vec2(0.0), wrapped);
    return max(cover.x, cover.y);
}

// ---- main ------------------------------------------------------------------
void main()
{
    float denom = vFar.y - vNear.y;
    if (abs(denom) < 1e-6) discard;
    float t = -vNear.y / denom;
    if (t < 0.0) discard;

    vec3 hit = vNear + t * (vFar - vNear);
    vec2 p   = hit.xz;

    // Write the true depth of the grid plane hit so geometry properly occludes it.
    vec4 clip = uVP * vec4(hit, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    // --- LOD ----------------------------------------------------------------
    float log10D  = log(uCamDist) / log(10.0);
    float floorD  = floor(log10D);
    float blend   = fract(log10D);

    float cellBase = pow(10.0, floorD - 1.0);
    float cell0 = cellBase * 1000.0;
    float cell1 = cellBase * 100.0;
    float cell2 = cellBase * 10.0;
    float cell3 = cellBase;

    float g0 = gridLine(p, cell0);
    float g1 = gridLine(p, cell1);
    float g2 = gridLine(p, cell2);
    float g3 = gridLine(p, cell3);
    g3 *= 1.0 - smoothstep(0.5, 1.0, blend);

    // --- axis lines ---------------------------------------------------------
    float axisX = smoothstep(fwidth(p.y) * 2.0, 0.0, abs(p.y));
    float axisZ = smoothstep(fwidth(p.x) * 2.0, 0.0, abs(p.x));

    // --- radial fade --------------------------------------------------------
    float dist2cam = length(p - uCamPos.xz);
    float fade     = 1.0 - smoothstep(uCamDist * 1.5, uCamDist * 7.0, dist2cam);
    if (fade < 0.001) discard;

    // ---- concrete tile texture ---------------------------------------------
    // Use the finest visible cell as the tile unit.
    // Inside each cell: smooth centre, noisy toward the edges (grout zone).
    float tileSize = cell2;   // medium LOD cell = one concrete tile

    // Position within cell, remapped to [0,1]
    vec2 cellUV = fract(p / tileSize);           // [0,1] within cell
    vec2 cellID = floor(p / tileSize);           // integer cell index

    // Distance from cell centre, remapped so 0=centre, 1=edge
    vec2 fromCentre = abs(cellUV - 0.5) * 2.0;  // [0,1], 0=centre 1=edge
    float edgeDist  = max(fromCentre.x, fromCentre.y);

    // Grout mask: 1 at the very edge of each tile, 0 in the interior.
    // Width of grout zone scales with tile size to stay consistent on screen.
    float groutWidth = 0.08;
    float groutMask  = smoothstep(1.0 - groutWidth, 1.0, edgeDist);

    // Per-tile unique value — gives each tile a slightly different tone
    float tileRnd = hash(cellID) * 0.5 + 0.5;   // [0.5, 1.0]

    // Interior noise: fbm sampled at a scale that gives nice concrete grain.
    // We scale the noise coords by tileSize so grain is physically consistent.
    float noiseScale = 4.0 / tileSize;
    float interiorNoise = fbm(p * noiseScale + cellID * 7.3);

    // Edge noise: finer, rougher noise to break up the grout line.
    float edgeNoise = fbm(p * noiseScale * 3.0 + cellID * 13.7);

    // Combine: interior is mostly smooth (low noise weight),
    //          edge zone ramps up noise so tiles look worn/rough at borders.
    float edgeWeight  = smoothstep(0.55, 1.0, edgeDist);  // 0 in centre, 1 at edge
    float noiseMix    = mix(interiorNoise * 0.12,          // gentle grain in middle
                            edgeNoise     * 0.55,          // heavy roughness at edge
                            edgeWeight);

    // Base concrete brightness (per-tile variation + noise)
    float concrete = tileRnd * 0.82 + noiseMix;
    concrete = clamp(concrete, 0.0, 1.0);

    // Grout is darker and rougher than the tile face
    float grout = clamp(edgeNoise * 0.4 + 0.08, 0.0, 1.0);

    // Blend tile face with grout line
    float surface = mix(concrete, grout, groutMask);

    // Map to a concrete colour range: light blue-grey
    vec3 concreteLight = vec3(0.62, 0.68, 0.78);   // light blue-grey tile face
    vec3 concreteDark  = vec3(0.28, 0.33, 0.42);   // darker blue-grey grout/shadow
    vec3 surfaceCol    = mix(concreteDark, concreteLight, surface);

    // ---- grid lines --------------------------------------------------------
    vec3 col0c = vec3(0.38, 0.39, 0.43);
    vec3 col1c = vec3(0.30, 0.31, 0.36);
    vec3 col2c = vec3(0.24, 0.25, 0.29);
    vec3 col3c = vec3(0.19, 0.20, 0.23);
    vec3 colX  = vec3(0.70, 0.14, 0.14);
    vec3 colZ  = vec3(0.14, 0.30, 0.70);

    // Start with the concrete surface as the base colour
    vec3  col   = surfaceCol;
    float alpha = 0.72;   // tile is semi-transparent so scene bg shows through slightly

    // Overlay grid lines on top
    col = mix(col, col3c, g3); alpha = max(alpha, g3);
    col = mix(col, col2c, g2); alpha = max(alpha, g2);
    col = mix(col, col1c, g1); alpha = max(alpha, g1);
    col = mix(col, col0c, g0); alpha = max(alpha, g0);
    col = mix(col, colX,  axisX); alpha = max(alpha, axisX);
    col = mix(col, colZ,  axisZ); alpha = max(alpha, axisZ);

    alpha *= fade;
    if (alpha < 0.005) discard;

    FragColor = vec4(col, alpha);
}
)GLSL";


static const char* GHOST_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)GLSL";

static const char* GHOST_FRAG = R"GLSL(
#version 330 core
uniform vec4 uColor;   // rgb + alpha
out vec4 FragColor;
void main()
{
    FragColor = uColor;
}
)GLSL";
// ============================================================
// Shader compiler
// ============================================================

static GLuint compileShader(GLenum type, const char* src, const char* name)
{
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        std::cerr << "[Shader] Compile error in " << name << ":\n" << log << "\n";
    }
    return id;
}

static GLuint buildProgram(const char* vertSrc, const char* fragSrc, const char* name)
{
    GLuint vert = compileShader(GL_VERTEX_SHADER,   vertSrc, name);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc, name);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "[Shader] Link error (" << name << "): " << log << "\n";
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

// ============================================================
// Camera
// ============================================================

glm::vec3 Camera::position() const
{
    float y = glm::radians(yaw);
    float p = glm::radians(pitch);
    return target + glm::vec3(cosf(p)*cosf(y), sinf(p), cosf(p)*sinf(y)) * dist;
}

glm::mat4 Camera::viewMatrix() const
{
    return glm::lookAt(position(), target, glm::vec3(0,1,0));
}

glm::mat4 Camera::projMatrix(float aspect) const
{
    return glm::perspective(glm::radians(45.f), aspect, 0.01f, 500.f);
}

void Camera::orbit(float dYaw, float dPitch)
{
    yaw   += dYaw;
    pitch += dPitch;
    if (pitch >  89.f) pitch =  89.f;
    if (pitch < -89.f) pitch = -89.f;
}

void Camera::pan(float dx, float dy)
{
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    glm::vec3 fwd(-cosf(pr)*cosf(yr), -sinf(pr), -cosf(pr)*sinf(yr));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0,1,0)));
    glm::vec3 up    = glm::normalize(glm::cross(right, fwd));
    float spd = dist * 0.002f;
    target -= right * (dx * spd);
    target += up    * (dy * spd);
}

void Camera::zoom(float delta)
{
    // Proportional zoom — feels natural at any distance
    dist -= delta * (dist * 0.12f);
    if (dist < 0.05f) dist = 0.05f;
    if (dist > 400.f) dist = 400.f;
}


std::pair<glm::vec3,glm::vec3> Camera::screenRay(
    float sx, float sy, int vpW, int vpH) const
{
    // Convert pixel to NDC [-1,1]
    float ndcX =  (2.f * sx) / (float)vpW - 1.f;
    float ndcY = -(2.f * sy) / (float)vpH + 1.f;   // flip Y (screen Y down, NDC Y up)

    float aspect = vpW > 0 ? (float)vpW / (float)vpH : 1.f;
    glm::mat4 proj = projMatrix(aspect);
    glm::mat4 view = viewMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, -1.f, 1.f);
    glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY,  1.f, 1.f);
    glm::vec3 nearW = glm::vec3(near4) / near4.w;
    glm::vec3 farW  = glm::vec3(far4)  / far4.w;

    glm::vec3 dir = glm::normalize(farW - nearW);
    return { nearW, dir };
}
// ============================================================
// GpuMesh
// ============================================================
// Renderer
// ============================================================

bool Renderer::init()
{
    m_meshShader  = buildProgram(MESH_VERT,  MESH_FRAG,  "mesh");
    m_gridShader  = buildProgram(GRID_VERT,  GRID_FRAG,  "grid");
    m_ghostShader = buildProgram(GHOST_VERT, GHOST_FRAG, "ghost");

    if (!m_meshShader || !m_gridShader || !m_ghostShader) {
        std::cerr << "[Renderer] Shader build failed\n";
        return false;
    }

    buildCubeMesh();
    buildWireEdges();
    buildGrid();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    std::cout << "[Renderer] Init OK\n";
    return true;
}

void Renderer::shutdown()
{
    m_cubeMesh.destroy();
    if (m_gridVAO) { glDeleteVertexArrays(1, &m_gridVAO); m_gridVAO = 0; }
    if (m_gridVBO) { glDeleteBuffers(1, &m_gridVBO);      m_gridVBO = 0; }
    if (m_meshShader)  { glDeleteProgram(m_meshShader);  m_meshShader  = 0; }
    if (m_gridShader)  { glDeleteProgram(m_gridShader);  m_gridShader  = 0; }
    if (m_ghostShader) { glDeleteProgram(m_ghostShader); m_ghostShader = 0; }
    m_wireEdges.destroy();
}

void Renderer::beginFrame(int w, int h)
{
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.11f, 0.14f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() {}

void Renderer::drawGrid(const Camera& cam, int viewportW, int viewportH)
{
    float aspect = viewportH > 0 ? (float)viewportW / (float)viewportH : 1.f;
    glm::mat4 view = cam.viewMatrix();
    glm::mat4 proj = cam.projMatrix(aspect);
    glm::mat4 invVP = glm::inverse(proj * view);

    // Grid uses alpha blending for distance fade and anti-aliased lines.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Keep depth writes ON — the frag shader writes gl_FragDepth from the
    // actual Y=0 hit point so geometry correctly occludes the grid.

    glUseProgram(m_gridShader);
    glUniformMatrix4fv(glGetUniformLocation(m_gridShader, "uInvVP"),
                       1, GL_FALSE, glm::value_ptr(invVP));
    glUniformMatrix4fv(glGetUniformLocation(m_gridShader, "uVP"),
                       1, GL_FALSE, glm::value_ptr(proj * view));
    glUniform1f(glGetUniformLocation(m_gridShader, "uCamDist"), cam.dist);
    glm::vec3 cp = cam.position();
    glUniform3f(glGetUniformLocation(m_gridShader, "uCamPos"), cp.x, cp.y, cp.z);

    glBindVertexArray(m_gridVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);   // clip-space full-screen quad
    glBindVertexArray(0);
    glUseProgram(0);

    glDisable(GL_BLEND);
}

void Renderer::setMeshUniforms(GLuint shader, const Camera& cam,
                               const glm::mat4& model, const glm::vec3& color,
                               int vpW, int vpH)
{
    float aspect        = vpH > 0 ? (float)vpW / (float)vpH : 1.f;
    glm::mat4 view      = cam.viewMatrix();
    glm::mat4 proj      = cam.projMatrix(aspect);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));

    glUniformMatrix4fv(glGetUniformLocation(shader, "uModel"),        1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "uView"),         1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "uProjection"),   1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix3fv(glGetUniformLocation(shader, "uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMat));
    glUniform3fv(glGetUniformLocation(shader, "uColor"),    1, glm::value_ptr(color));
    glUniform3f(glGetUniformLocation(shader,  "uLightPos"), 8.f, 15.f, 10.f);
    glUniform3fv(glGetUniformLocation(shader, "uViewPos"),  1, glm::value_ptr(cam.position()));
}

void Renderer::drawMesh(const Camera& cam, const GpuMesh& mesh,
                        const glm::mat4& model, const glm::vec3& color,
                        int vpW, int vpH)
{
    glUseProgram(m_meshShader);
    setMeshUniforms(m_meshShader, cam, model, color, vpW, vpH);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
}

void Renderer::drawCube(const Camera& cam, const glm::mat4& model,
                        const glm::vec3& color, int viewportW, int viewportH)
{
    drawMesh(cam, m_cubeMesh, model, color, viewportW, viewportH);
}

void Renderer::drawSceneObject(const Camera& cam, const SceneObject& obj,
                               int viewportW, int viewportH)
{
    if (!obj.visible) return;

    glm::mat4 model = obj.transform();
    float tint = obj.selected ? 1.7f : (obj.hovered ? 1.3f : 1.0f);

    if (obj.mesh && obj.mesh->isLoaded()) {
        const MeshAsset& asset = *obj.mesh;

        if (!asset.submeshes.empty()) {
            // Multi-material mesh — draw each submesh with its own colour
            glUseProgram(m_meshShader);
            setMeshUniforms(m_meshShader, cam, model, {1,1,1}, viewportW, viewportH);
            glBindVertexArray(asset.gpu.vao);
            for (const SubMesh& sm : asset.submeshes) {
                glm::vec3 col = glm::min(sm.color * tint, glm::vec3(1.f));
                glUniform3fv(glGetUniformLocation(m_meshShader, "uColor"), 1,
                             glm::value_ptr(col));
                glDrawElements(GL_TRIANGLES, sm.indexCount, GL_UNSIGNED_INT,
                               (void*)(uintptr_t)sm.indexOffset);
            }
            glBindVertexArray(0);
            glUseProgram(0);
        } else {
            glm::vec3 col = glm::min(obj.color * tint, glm::vec3(1.f));
            drawMesh(cam, asset.gpu, model, col, viewportW, viewportH);
        }
    } else {
        glm::vec3 col = glm::min(obj.color * tint, glm::vec3(1.f));
        drawMesh(cam, m_cubeMesh, model, col, viewportW, viewportH);
    }
}

// ============================================================
// Private builders
// ============================================================

void Renderer::buildCubeMesh()
{
    // 24 vertices — 4 unique per face so each face has a clean flat normal
    static const float verts[] = {
        // pos               normal
        -0.5f,-0.5f, 0.5f,  0, 0, 1,   // +Z face
         0.5f,-0.5f, 0.5f,  0, 0, 1,
         0.5f, 0.5f, 0.5f,  0, 0, 1,
        -0.5f, 0.5f, 0.5f,  0, 0, 1,

         0.5f,-0.5f,-0.5f,  0, 0,-1,   // -Z face
        -0.5f,-0.5f,-0.5f,  0, 0,-1,
        -0.5f, 0.5f,-0.5f,  0, 0,-1,
         0.5f, 0.5f,-0.5f,  0, 0,-1,

        -0.5f, 0.5f, 0.5f,  0, 1, 0,   // +Y face
         0.5f, 0.5f, 0.5f,  0, 1, 0,
         0.5f, 0.5f,-0.5f,  0, 1, 0,
        -0.5f, 0.5f,-0.5f,  0, 1, 0,

         0.5f,-0.5f, 0.5f,  0,-1, 0,   // -Y face
        -0.5f,-0.5f, 0.5f,  0,-1, 0,
        -0.5f,-0.5f,-0.5f,  0,-1, 0,
         0.5f,-0.5f,-0.5f,  0,-1, 0,

         0.5f,-0.5f, 0.5f,  1, 0, 0,   // +X face
         0.5f,-0.5f,-0.5f,  1, 0, 0,
         0.5f, 0.5f,-0.5f,  1, 0, 0,
         0.5f, 0.5f, 0.5f,  1, 0, 0,

        -0.5f,-0.5f,-0.5f, -1, 0, 0,   // -X face
        -0.5f,-0.5f, 0.5f, -1, 0, 0,
        -0.5f, 0.5f, 0.5f, -1, 0, 0,
        -0.5f, 0.5f,-0.5f, -1, 0, 0,
    };

    static const unsigned int idx[] = {
         0, 1, 2,  2, 3, 0,
         4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20,
    };

    glGenVertexArrays(1, &m_cubeMesh.vao);
    glGenBuffers(1, &m_cubeMesh.vbo);
    glGenBuffers(1, &m_cubeMesh.ebo);
    glBindVertexArray(m_cubeMesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeMesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeMesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    m_cubeMesh.indexCount = 36;
}

void Renderer::buildGrid()
{
    // A clip-space full-screen quad (TRIANGLE_STRIP: TL, BL, TR, BR).
    // The vertex shader unprojcts each corner to world space so the fragment
    // shader can work entirely in world XZ coordinates — no geometry needed.
    static const float quad[] = {
        -1.f,  1.f,   // top-left
        -1.f, -1.f,   // bottom-left
         1.f,  1.f,   // top-right
         1.f, -1.f,   // bottom-right
    };

    m_gridLineCount = 4;  // repurposed as vertex count

    glGenVertexArrays(1, &m_gridVAO);
    glGenBuffers(1, &m_gridVBO);
    glBindVertexArray(m_gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}


void Renderer::drawGhostCube(const Camera& cam, const glm::mat4& model,
                              const glm::vec3& color, float alpha,
                              int viewportW, int viewportH)
{
    float aspect = viewportH > 0 ? (float)viewportW / (float)viewportH : 1.f;
    glm::mat4 view = cam.viewMatrix();
    glm::mat4 proj = cam.projMatrix(aspect);

    glUseProgram(m_ghostShader);
    glUniformMatrix4fv(glGetUniformLocation(m_ghostShader, "uModel"),      1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(m_ghostShader, "uView"),       1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_ghostShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(proj));

    // --- Transparent fill ---
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);          // don't write to depth — ghost passes through
    glDisable(GL_CULL_FACE);        // see all faces of ghost

    glUniform4f(glGetUniformLocation(m_ghostShader, "uColor"),
                color.r, color.g, color.b, alpha * 0.25f);  // very faint fill

    glBindVertexArray(m_cubeMesh.vao);
    glDrawElements(GL_TRIANGLES, m_cubeMesh.indexCount, GL_UNSIGNED_INT, nullptr);

    // --- Wireframe edges — brighter, full alpha ---
    glUniform4f(glGetUniformLocation(m_ghostShader, "uColor"),
                color.r, color.g, color.b, alpha);

    glLineWidth(1.5f);
    glBindVertexArray(m_wireEdges.vao);
    glDrawElements(GL_LINES, m_wireEdges.indexCount, GL_UNSIGNED_INT, nullptr);

    // Restore state
    glBindVertexArray(0);
    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void Renderer::buildWireEdges()
{
    // 8 corners of a unit cube centred at origin
    static const float verts[] = {
        -0.5f,-0.5f,-0.5f,   // 0 LBB
         0.5f,-0.5f,-0.5f,   // 1 RBB
         0.5f, 0.5f,-0.5f,   // 2 RTB
        -0.5f, 0.5f,-0.5f,   // 3 LTB
        -0.5f,-0.5f, 0.5f,   // 4 LBF
         0.5f,-0.5f, 0.5f,   // 5 RBF
         0.5f, 0.5f, 0.5f,   // 6 RTF
        -0.5f, 0.5f, 0.5f,   // 7 LTF
    };
    // 12 edges
    static const unsigned int idx[] = {
        0,1, 1,2, 2,3, 3,0,   // back face
        4,5, 5,6, 6,7, 7,4,   // front face
        0,4, 1,5, 2,6, 3,7,   // connecting edges
    };

    glGenVertexArrays(1, &m_wireEdges.vao);
    glGenBuffers(1, &m_wireEdges.vbo);
    glGenBuffers(1, &m_wireEdges.ebo);

    glBindVertexArray(m_wireEdges.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_wireEdges.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_wireEdges.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    m_wireEdges.indexCount = 24;  // 12 edges × 2 vertices
}

#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

// ---- CPU-side mesh data ----------------------------------------------------
// Stored after import so we can re-upload, inspect, or export later.
struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<MeshVertex>   vertices;
    std::vector<unsigned int> indices;

    // Axis-aligned bounding box — computed on import
    glm::vec3 aabbMin = { 1e9f, 1e9f, 1e9f};
    glm::vec3 aabbMax = {-1e9f,-1e9f,-1e9f};

    void computeAABB();
    glm::vec3 centre() const { return (aabbMin + aabbMax) * 0.5f; }
    glm::vec3 size()   const { return  aabbMax - aabbMin; }
};

// ---- GPU-side mesh ---------------------------------------------------------
struct GpuMesh
{
    GLuint vao        = 0;
    GLuint vbo        = 0;
    GLuint ebo        = 0;
    int    indexCount = 0;
    void destroy();
};

// ---- SubMesh ---------------------------------------------------------------
// A material group within a MeshAsset — contiguous index range with one colour.
// Populated by GltfImporter (baseColorFactor) and ObjImporter (Kd) when present.
struct SubMesh {
    std::string  materialName;
    glm::vec3    color  = {0.75f, 0.75f, 0.75f};
    int          indexOffset = 0;  // byte offset into IBO
    int          indexCount  = 0;
};

// ---- MeshAsset -------------------------------------------------------------
// One named mesh that can be shared by many SceneObjects.
// Owns both CPU data and the uploaded GpuMesh.
//
// Sources:
//   - "cube:<color>"  — procedural placeholder, generated in code
//   - "obj:<path>"    — imported from an OBJ file
struct MeshAsset {
    std::string  name;       // unique key in MeshLibrary
    std::string  sourcePath; // empty for procedural

    MeshData  data;
    GpuMesh   gpu;

    // Per-material submeshes — empty means single-colour using SceneObject::color.
    std::vector<SubMesh> submeshes;

    bool isLoaded() const { return gpu.vao != 0; }

    // Upload CPU data → GPU. Call after data is filled.
    bool upload();

    // Free GPU resources (CPU data kept for re-upload / export)
    void unload();

    ~MeshAsset() { unload(); }

    // Non-copyable — owns GPU resources
    MeshAsset()                            = default;
    MeshAsset(const MeshAsset&)            = delete;
    MeshAsset& operator=(const MeshAsset&) = delete;
    MeshAsset(MeshAsset&&)                 = default;
};

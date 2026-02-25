#pragma once
#include "MeshAsset.h"
#include <string>
#include <memory>

// Loads a Wavefront OBJ file into a MeshAsset.
//
// Supports:
//   - v  (positions)
//   - vn (normals)
//   - vt (UVs)
//   - f  (faces — triangles and quads, auto-triangulated)
//   - o / g (object/group names — ignored, all geometry merged)
//   - #  (comments — ignored)
//   - mtllib / usemtl — ignored for now (colour comes from SceneObject)
//
// Does NOT support:
//   - Multiple objects in one file (all merged into one mesh)
//   - Sub-meshes or material groups (planned for Phase 10)
//   - Relative indices (negative face indices in OBJ spec)

class ObjImporter
{
public:
    // Returns nullptr on failure. Logs errors to stderr.
    // Does NOT upload to GPU — call asset->upload() after.
    static std::shared_ptr<MeshAsset> load(const std::string& path);

private:
    static void computeFlatNormals(MeshData& data);
};

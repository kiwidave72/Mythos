#pragma once
#include "MeshAsset.h"
#include <string>
#include <memory>

// Imports .gltf (JSON + external .bin) and .glb (self-contained binary) files.
// Blender exports both formats cleanly. Reads POSITION, NORMAL, TEXCOORD_0
// attributes and pbrMetallicRoughness baseColorFactor for per-submesh colouring.

class GltfImporter
{
public:
    static std::shared_ptr<MeshAsset> load(const std::string& path);

private:
    static void computeFlatNormals(MeshData& data);
};

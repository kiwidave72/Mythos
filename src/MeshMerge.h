#pragma once
#include "MeshAsset.h"
#include "SceneObject.h"
#include <vector>
#include <memory>
#include <string>

// ---- MeshMerge -------------------------------------------------------------
// Utilities for merging multiple SceneObjects into a single MeshAsset.
//
// Materials are preserved: each source object contributes one SubMesh entry
// in the output, carrying its colour. Objects with existing submeshes get one
// output SubMesh per source SubMesh, so multi-material assets are fully kept.
//
// Welded merge additionally collapses vertices within epsilon world units.
// Note: welding regenerates indices so per-SubMesh index ranges are recomputed.
namespace MeshMerge
{
    // Result of a merge — ready to upload after calling asset->upload().
    struct Result {
        std::shared_ptr<MeshAsset> asset;   // filled vertex/index/submesh data
        std::string                name;    // suggested name
    };

    // Merge objects preserving materials as SubMeshes.
    Result merge(const std::vector<const SceneObject*>& objects,
                 const std::string& name);

    // Merge + weld shared vertices (watertight seams). epsilon in world units.
    Result mergeAndWeld(const std::vector<const SceneObject*>& objects,
                        const std::string& name,
                        float epsilon = 0.001f);

    // Weld an already-merged MeshData in place (recomputes SubMesh ranges).
    void weld(MeshData& data, std::vector<SubMesh>& submeshes,
              float epsilon = 0.001f);
}

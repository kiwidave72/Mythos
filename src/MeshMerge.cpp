#include "MeshMerge.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace MeshMerge
{

// ============================================================
// Internal helpers
// ============================================================

static MeshVertex transformVertex(const MeshVertex& v,
                                  const glm::mat4& M, const glm::mat3& NM)
{
    MeshVertex out;
    out.pos    = glm::vec3(M * glm::vec4(v.pos, 1.f));
    out.normal = glm::normalize(NM * v.normal);
    out.uv     = v.uv;
    return out;
}

// Append one index range from src into dst, building a new SubMesh descriptor.
static SubMesh appendRange(MeshData& dst,
                            const MeshData& src,
                            const glm::mat4& M, const glm::mat3& NM,
                            unsigned int indexBegin, unsigned int indexCount,
                            const std::string& matName, const glm::vec3& color)
{
    // Remap: only vertices referenced in [indexBegin, indexBegin+indexCount)
    std::unordered_map<unsigned int, unsigned int> remap;
    remap.reserve(indexCount);

    unsigned int baseIndex = (unsigned int)dst.indices.size();

    for (unsigned int i = indexBegin; i < indexBegin + indexCount; ++i) {
        unsigned int si = src.indices[i];
        auto it = remap.find(si);
        if (it == remap.end()) {
            unsigned int ni = (unsigned int)dst.vertices.size();
            remap[si] = ni;
            dst.vertices.push_back(transformVertex(src.vertices[si], M, NM));
            dst.indices.push_back(ni);
        } else {
            dst.indices.push_back(it->second);
        }
    }

    SubMesh sm;
    sm.materialName = matName;
    sm.color        = color;
    sm.indexOffset  = (int)(baseIndex * sizeof(unsigned int));
    sm.indexCount   = (int)indexCount;
    return sm;
}

// ============================================================
// merge
// ============================================================

Result merge(const std::vector<const SceneObject*>& objects,
             const std::string& name)
{
    Result res;
    res.name  = name;
    res.asset = std::make_shared<MeshAsset>();
    res.asset->name = name;

    MeshData&             data      = res.asset->data;
    std::vector<SubMesh>& submeshes = res.asset->submeshes;

    for (const SceneObject* obj : objects) {
        if (!obj || !obj->mesh) continue;

        const MeshData& src = obj->mesh->data;
        glm::mat4 M  = obj->transform();
        glm::mat3 NM = glm::mat3(glm::transpose(glm::inverse(M)));

        if (!obj->mesh->submeshes.empty()) {
            // Multi-material: emit one output SubMesh per source SubMesh
            for (const SubMesh& ssm : obj->mesh->submeshes) {
                unsigned int begin = (unsigned int)(ssm.indexOffset / sizeof(unsigned int));
                submeshes.push_back(
                    appendRange(data, src, M, NM,
                                begin, (unsigned int)ssm.indexCount,
                                ssm.materialName, ssm.color));
            }
        } else {
            // Single colour: emit one SubMesh carrying obj->color
            submeshes.push_back(
                appendRange(data, src, M, NM,
                            0, (unsigned int)src.indices.size(),
                            obj->name, obj->color));
        }
    }

    // Collapse to no-submesh if everything is the same colour
    if (!submeshes.empty()) {
        bool allSame = true;
        const glm::vec3& c0 = submeshes[0].color;
        for (auto& sm : submeshes)
            if (glm::distance(sm.color, c0) > 0.01f) { allSame = false; break; }
        if (allSame) submeshes.clear();
    }

    data.computeAABB();
    return res;
}

// ============================================================
// weld
// ============================================================

struct GridKey {
    int x, y, z;
    bool operator==(const GridKey& o) const { return x==o.x && y==o.y && z==o.z; }
};
struct GridKeyHash {
    size_t operator()(const GridKey& k) const {
        size_t h = 2166136261u;
        auto mix = [&](int v){ h ^= (size_t)(unsigned)v; h *= 16777619u; };
        mix(k.x); mix(k.y); mix(k.z); return h;
    }
};

void weld(MeshData& data, std::vector<SubMesh>& submeshes, float epsilon)
{
    if (data.vertices.empty()) return;
    float inv = 1.f / epsilon;

    std::unordered_map<GridKey, unsigned int, GridKeyHash> grid;
    grid.reserve(data.vertices.size());

    std::vector<unsigned int> remap(data.vertices.size());
    std::vector<MeshVertex>   welded;
    welded.reserve(data.vertices.size());

    for (size_t i = 0; i < data.vertices.size(); ++i) {
        const glm::vec3& p = data.vertices[i].pos;
        GridKey key {
            (int)std::floor(p.x * inv + 0.5f),
            (int)std::floor(p.y * inv + 0.5f),
            (int)std::floor(p.z * inv + 0.5f)
        };
        auto it = grid.find(key);
        if (it != grid.end()) { remap[i] = it->second; }
        else {
            unsigned int ni = (unsigned int)welded.size();
            grid[key] = ni; remap[i] = ni;
            welded.push_back(data.vertices[i]);
        }
    }

    // Build submesh ranges as flat index positions
    struct Range { int begin; int end; int smIdx; };
    std::vector<Range> ranges;
    for (int i = 0; i < (int)submeshes.size(); ++i) {
        int b = (int)(submeshes[i].indexOffset / sizeof(unsigned int));
        ranges.push_back({ b, b + submeshes[i].indexCount, i });
    }
    for (auto& sm : submeshes) { sm.indexCount = 0; }

    std::vector<unsigned int> newIdx;
    newIdx.reserve(data.indices.size());

    for (int t = 0; t + 2 < (int)data.indices.size(); t += 3) {
        unsigned int a = remap[data.indices[t+0]];
        unsigned int b = remap[data.indices[t+1]];
        unsigned int c = remap[data.indices[t+2]];
        if (a == b || b == c || a == c) continue;

        if (!ranges.empty()) {
            for (auto& r : ranges) {
                if (t >= r.begin && t < r.end) {
                    submeshes[r.smIdx].indexCount += 3; break;
                }
            }
        }
        newIdx.push_back(a); newIdx.push_back(b); newIdx.push_back(c);
    }

    // Recompute sequential offsets
    int offset = 0;
    for (auto& sm : submeshes) {
        sm.indexOffset = (int)(offset * sizeof(unsigned int));
        offset += sm.indexCount;
    }

    data.vertices = std::move(welded);
    data.indices  = std::move(newIdx);
    data.computeAABB();
}

Result mergeAndWeld(const std::vector<const SceneObject*>& objects,
                    const std::string& name, float epsilon)
{
    Result res = merge(objects, name);
    weld(res.asset->data, res.asset->submeshes, epsilon);
    return res;
}

} // namespace MeshMerge

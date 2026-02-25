#include "Scene.h"
#include "AssetLibrary.h"
#include "../lib/grammar-core/Grammar.h"
#include <iostream>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// InducedGenerator is an optional component — include only if present.
// If your project has lib/InducedGenerator.h, uncomment the next line:
// #include "InducedGenerator.h"

// ============================================================
// MeshLibrary
// ============================================================

std::shared_ptr<MeshAsset> MeshLibrary::getOrCreateCube(
    const std::string& primId, const glm::vec3& /*color*/)
{
    auto oit = m_primOverrides.find(primId);
    if (oit != m_primOverrides.end()) return oit->second;

    std::string key = "cube:" + primId;
    auto it = m_assets.find(key);
    if (it != m_assets.end()) return it->second;

    auto asset = makeCube(key);
    m_assets[key] = asset;
    return asset;
}

std::shared_ptr<MeshAsset> MeshLibrary::importObj(const std::string& path)
{
    auto asset = ObjImporter::load(path);
    if (!asset) return nullptr;
    if (!asset->upload()) return nullptr;
    m_assets[asset->name] = asset;
    std::cout << "[MeshLibrary] Imported: " << asset->name << "\n";
    return asset;
}

void MeshLibrary::assignObjToPrim(const std::string& primId,
                                   std::shared_ptr<MeshAsset> asset)
{
    m_primOverrides[primId] = asset;
    std::cout << "[MeshLibrary] Assigned '" << asset->name
              << "' to prim '" << primId << "'\n";
}

std::shared_ptr<MeshAsset> MeshLibrary::find(const std::string& name) const
{
    auto it = m_assets.find(name);
    return (it != m_assets.end()) ? it->second : nullptr;
}

std::shared_ptr<MeshAsset> MeshLibrary::makeCube(const std::string& name)
{
    auto asset = std::make_shared<MeshAsset>();
    asset->name = name;

    struct FaceData { glm::vec3 normal; glm::vec3 verts[4]; glm::vec2 uvs[4]; };
    static const FaceData faces[] = {
        {{0,0,1},  {{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},   {{0,0},{1,0},{1,1},{0,1}}},
        {{0,0,-1}, {{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f}},{{0,0},{1,0},{1,1},{0,1}}},
        {{0,1,0},  {{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f}},    {{0,0},{1,0},{1,1},{0,1}}},
        {{0,-1,0}, {{.5f,-.5f,.5f},{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f}},{{0,0},{1,0},{1,1},{0,1}}},
        {{1,0,0},  {{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f}},    {{0,0},{1,0},{1,1},{0,1}}},
        {{-1,0,0}, {{-.5f,-.5f,-.5f},{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f}},{{0,0},{1,0},{1,1},{0,1}}},
    };

    for (auto& f : faces) {
        unsigned int base = (unsigned int)asset->data.vertices.size();
        for (int i = 0; i < 4; ++i)
            asset->data.vertices.push_back({f.verts[i], f.normal, f.uvs[i]});
        asset->data.indices.insert(asset->data.indices.end(),
            {base, base+1, base+2,  base+2, base+3, base});
    }

    asset->data.computeAABB();
    asset->upload();
    return asset;
}

// ============================================================
// Scene — object management
// ============================================================

SceneObject& Scene::addObject()
{
    SceneObject obj;
    obj.id = m_nextId++;
    m_objects.push_back(std::move(obj));
    return m_objects.back();
}

void Scene::removeObject(int id)
{
    m_objects.erase(
        std::remove_if(m_objects.begin(), m_objects.end(),
                       [id](const SceneObject& o){ return o.id == id; }),
        m_objects.end());

    if (m_selectedId == id) m_selectedId = -1;
    if (m_hoveredId  == id) m_hoveredId  = -1;

    m_selectedIds.erase(
        std::remove(m_selectedIds.begin(), m_selectedIds.end(), id),
        m_selectedIds.end());

    // Rebuild cell map
    m_cellToId.clear();
    for (auto& o : m_objects)
        m_cellToId[{o.gridCell.x, o.gridCell.y}] = o.id;

    syncSelectedFlag();
}

void Scene::clear()
{
    m_objects.clear();
    m_cellToId.clear();
    m_selectedId  = -1;
    m_hoveredId   = -1;
    m_selectedIds.clear();
    m_nextId      = 1;
}

SceneObject* Scene::findById(int id)
{
    for (auto& o : m_objects)
        if (o.id == id) return &o;
    return nullptr;
}

const SceneObject* Scene::findById(int id) const
{
    for (const auto& o : m_objects)
        if (o.id == id) return &o;
    return nullptr;
}

// ============================================================
// Scene — single selection
// ============================================================

void Scene::selectNone()
{
    m_selectedId = -1;
    m_selectedIds.clear();
    syncSelectedFlag();
}

void Scene::selectById(int id)
{
    m_selectedId  = id;
    m_selectedIds = { id };
    syncSelectedFlag();
}

// ============================================================
// Scene — multi-selection
// ============================================================

void Scene::selectAdd(int id)
{
    if (std::find(m_selectedIds.begin(), m_selectedIds.end(), id)
            == m_selectedIds.end())
        m_selectedIds.push_back(id);
    m_selectedId = id;   // clicked one becomes primary
    syncSelectedFlag();
}

void Scene::deselectById(int id)
{
    m_selectedIds.erase(
        std::remove(m_selectedIds.begin(), m_selectedIds.end(), id),
        m_selectedIds.end());
    if (m_selectedId == id)
        m_selectedId = m_selectedIds.empty() ? -1 : m_selectedIds.back();
    syncSelectedFlag();
}

void Scene::selectAll()
{
    m_selectedIds.clear();
    for (auto& o : m_objects)
        m_selectedIds.push_back(o.id);
    m_selectedId = m_selectedIds.empty() ? -1 : m_selectedIds.back();
    syncSelectedFlag();
}

bool Scene::isSelected(int id) const
{
    return std::find(m_selectedIds.begin(), m_selectedIds.end(), id)
           != m_selectedIds.end();
}

void Scene::syncSelectedFlag()
{
    for (auto& o : m_objects)
        o.selected = isSelected(o.id);
}

// ============================================================
// Scene — ray-OBB picking
// ============================================================
// We test the ray against each object's local-space AABB by transforming the
// ray into local space (via the inverse of the object's world matrix).
// This turns the OBB test into a simple AABB slab test.

int Scene::pickObject(const glm::vec3& rayOrig, const glm::vec3& rayDir) const
{
    float bestT   = 1e30f;
    int   bestId  = -1;

    for (const auto& obj : m_objects)
    {
        if (!obj.visible) continue;
        if (!obj.mesh)    continue;

        const glm::vec3& bmin = obj.mesh->data.aabbMin;
        const glm::vec3& bmax = obj.mesh->data.aabbMax;

        // Skip degenerate (un-computed) AABBs
        if (bmin.x > bmax.x) continue;

        // Transform ray into object local space
        glm::mat4 invM = glm::inverse(obj.transform());
        glm::vec3 ro   = glm::vec3(invM * glm::vec4(rayOrig, 1.f));
        glm::vec3 rd   = glm::vec3(invM * glm::vec4(rayDir,  0.f));

        // Slab test against local AABB
        float tmin = 0.f, tmax = 1e30f;
        for (int axis = 0; axis < 3; ++axis)
        {
            float origin = (&ro.x)[axis];
            float dir    = (&rd.x)[axis];
            float lo     = (&bmin.x)[axis];
            float hi     = (&bmax.x)[axis];

            if (std::abs(dir) < 1e-7f) {
                // Ray parallel to slab — check if origin is inside
                if (origin < lo || origin > hi) { tmin = 2e30f; break; }
            } else {
                float t1 = (lo - origin) / dir;
                float t2 = (hi - origin) / dir;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) break;
            }
        }

        if (tmin > tmax || tmax < 0.f) continue;  // miss
        float t = (tmin >= 0.f) ? tmin : tmax;

        // Convert t back to world space (approximate: use scale of transform)
        // We compare in local space — objects with non-uniform scale are fine
        // because we just want the nearest in pick order.
        if (t < bestT) {
            bestT  = t;
            bestId = obj.id;
        }
    }

    return bestId;
}

// ============================================================
// Scene — grid / cursor
// ============================================================

int Scene::objectAtCell(glm::ivec2 cell) const
{
    auto it = m_cellToId.find({cell.x, cell.y});
    return (it != m_cellToId.end()) ? it->second : -1;
}

void Scene::setCursorCell(glm::ivec2 cell, bool valid)
{
    if (m_hoveredId != -1) {
        if (auto* o = findById(m_hoveredId)) o->hovered = false;
        m_hoveredId = -1;
    }
    if (!valid) return;
    int id = objectAtCell(cell);
    if (id == -1) return;
    m_hoveredId = id;
    if (auto* o = findById(id)) o->hovered = true;
}

void Scene::rebuildCellMap()
{
    m_cellToId.clear();
    for (auto& o : m_objects)
        m_cellToId[{o.gridCell.x, o.gridCell.y}] = o.id;
}

// ============================================================
// Grammar integration
// ============================================================

void Scene::populateFromGrammar(const grammar::Grammar& gram, MeshLibrary& lib)
{
    clear();

    if (gram.placed.empty()) return;

    // Assign colours — cycle through a palette
    static const glm::vec3 palette[] = {
        {0.30f,0.55f,0.90f}, {0.85f,0.35f,0.25f}, {0.25f,0.75f,0.45f},
        {0.90f,0.75f,0.20f}, {0.70f,0.30f,0.80f}, {0.20f,0.75f,0.85f},
    };
    int ci = 0;

    for (const auto& p : gram.placed) {
        if (!p.def) continue;
        const std::string& primId = p.def->id;
        const glm::vec3&   color  = palette[ci % 6];

        auto mesh = lib.getOrCreateCube(primId, color);

        SceneObject& obj = addObject();
        obj.name     = primId;
        obj.primId   = primId;
        obj.mesh     = mesh;
        obj.color    = color;
        obj.position = glm::vec3((float)p.cell.x, 0.f, (float)p.cell.y);
        obj.rotation = glm::vec3(0.f, -(float)p.rot, 0.f);
        obj.scale    = glm::vec3(1.f, 0.5f, 1.f);
        obj.gridCell = p.cell;
        ++ci;

        m_cellToId[{p.cell.x, p.cell.y}] = obj.id;
    }
}

void Scene::populateFromInduced(const grammar::InducedGenerator& /*gen*/,
                                 AssetLibrary& /*assetLib*/)
{
    // Full implementation requires InducedGenerator.h from lib/.
    // If your project has that file, replace Scene.cpp with the original version
    // from your repository — this stub is provided so the project compiles without it.
    std::cout << "[Scene] populateFromInduced: InducedGenerator not linked in this build.\n";
}

int Scene::importObj(const std::string& path, MeshLibrary& lib)
{
    auto mesh = lib.importObj(path);
    if (!mesh) return -1;

    SceneObject& obj = addObject();
    obj.name   = mesh->name;
    obj.primId = mesh->name;
    obj.mesh   = mesh;
    obj.color  = {0.75f, 0.78f, 0.85f};
    return obj.id;
}

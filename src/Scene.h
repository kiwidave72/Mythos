#pragma once
#include "SceneObject.h"
#include "MeshAsset.h"
#include "ObjImporter.h"
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <functional>

// Forward declarations — full headers included in Scene.cpp only
namespace grammar { class Grammar; }
namespace grammar { class InducedGenerator; }
class AssetLibrary;

// Grid cell size in world units — grammar pieces are 1×1, assets scale to match.
static constexpr float kGridCell = 1.0f;

// ---- MeshLibrary -----------------------------------------------------------
class MeshLibrary
{
public:
    std::shared_ptr<MeshAsset> getOrCreateCube(const std::string& primId,
                                               const glm::vec3& color);
    std::shared_ptr<MeshAsset> importObj(const std::string& path);
    void assignObjToPrim(const std::string& primId,
                         std::shared_ptr<MeshAsset> asset);
    std::shared_ptr<MeshAsset> find(const std::string& name) const;
    const std::map<std::string, std::shared_ptr<MeshAsset>>& all() const { return m_assets; }

private:
    std::map<std::string, std::shared_ptr<MeshAsset>> m_assets;
    std::map<std::string, std::shared_ptr<MeshAsset>> m_primOverrides;
    std::shared_ptr<MeshAsset> makeCube(const std::string& name);
};

// ---- Scene -----------------------------------------------------------------
class Scene
{
public:
    // --- Object management ---
    SceneObject& addObject();
    void         removeObject(int id);
    void         clear();

    SceneObject*       findById(int id);
    const SceneObject* findById(int id) const;

    const std::vector<SceneObject>& objects() const { return m_objects; }
          std::vector<SceneObject>& objects()       { return m_objects; }

    int objectCount() const { return (int)m_objects.size(); }

    // --- Grammar integration ---
    void populateFromGrammar(const grammar::Grammar& gram, MeshLibrary& lib);
    void populateFromInduced(const grammar::InducedGenerator& gen,
                             AssetLibrary& assetLib);

    // --- OBJ import ---
    int importObj(const std::string& path, MeshLibrary& lib);

    // --- Selection (single) ---
    void selectNone();
    void selectById(int id);
    int  selectedId() const { return m_selectedId; }

    // --- Multi-selection ---
    // selectAdd: add id to selection (keeps existing). If shift=false, replaces.
    void selectAdd(int id);
    void selectAll();
    void deselectById(int id);
    bool isSelected(int id) const;
    const std::vector<int>& selectedIds() const { return m_selectedIds; }
    int selectedCount() const { return (int)m_selectedIds.size(); }

    // --- Ray-OBB picking ---
    // Returns id of nearest object whose OBB the ray hits, or -1 if none.
    // rayOrig and rayDir must be in world space. rayDir need not be normalised.
    int pickObject(const glm::vec3& rayOrig, const glm::vec3& rayDir) const;

    // --- Grid lookup (grammar hover cursor) ---
    int  objectAtCell(glm::ivec2 cell) const;
    void setCursorCell(glm::ivec2 cell, bool valid);
    void rebuildCellMap();
    void setNextId(int id) { m_nextId = id; }

private:
    std::vector<SceneObject> m_objects;
    int m_nextId     = 1;
    int m_selectedId = -1;   // primary (drives gizmo pivot)
    int m_hoveredId  = -1;

    std::vector<int> m_selectedIds;  // all selected, including primary

    std::map<std::pair<int,int>, int> m_cellToId;

    void syncSelectedFlag();   // keeps SceneObject::selected in sync
};

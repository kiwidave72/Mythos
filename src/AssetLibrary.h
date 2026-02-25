#pragma once
#include "MeshAsset.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

// ---- AssetEntry ------------------------------------------------------------
// One imported OBJ in the editor's asset library.
// calibration corrects for axis convention differences between DCC tools.
// When placed into the Scene, the final transform is:
//   worldTransform = placementTransform * calibrationMatrix
struct AssetEntry {
    std::string name;         // display name (filename without extension)
    std::string sourcePath;   // original .obj path on disk

    std::shared_ptr<MeshAsset> mesh;

    // Calibration — applied to every instance placed into the scene.
    // Lets the user correct for Y-up vs Z-up, wrong scale, rotated root, etc.
    glm::vec3 calibPos    = {0.f, 0.f, 0.f};   // position offset
    glm::vec3 calibRot    = {0.f, 0.f, 0.f};   // Euler angles in degrees
    glm::vec3 calibScale  = {1.f, 1.f, 1.f};   // scale multiplier

    glm::mat4 calibMatrix() const;              // computed from above

    // Thumbnail GL texture (rendered on first draw, cached)
    GLuint thumbnailTex   = 0;
    bool   thumbDirty     = true;   // set true when calibration changes
};

// ---- AssetLibrary ----------------------------------------------------------
// The editor's persistent asset store.
// Lives in App alongside Scene. Completely separate from the Scene — the Scene
// holds placed instances, the library holds the source assets.
//
// Serialised to/from editor_assets.json next to the executable.
class AssetLibrary
{
public:
    // Load from disk on startup. Creates empty library if file not found.
    void load(const std::string& jsonPath);

    // Save to disk. Call on shutdown and after any change.
    void save(const std::string& jsonPath) const;

    // Import one or more OBJ files into the library.
    // Returns indices of newly added entries.
    std::vector<int> importObjs(const std::vector<std::string>& paths);

    // Remove an entry by index.
    void remove(int index);

    // All entries
    const std::vector<AssetEntry>& entries() const { return m_entries; }
          std::vector<AssetEntry>& entries()       { return m_entries; }

    int count() const { return (int)m_entries.size(); }

    // Path used for save/load (set by load(), used by save())
    std::string jsonPath;

private:
    std::vector<AssetEntry> m_entries;

    bool entryExists(const std::string& sourcePath) const;
};

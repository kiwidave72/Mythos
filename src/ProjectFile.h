#pragma once
#include "Scene.h"
#include "Renderer.h"
#include "../lib/grammar-ui/GrammarView.h"
#include <string>

// ---- ProjectFile -----------------------------------------------------------
// Saves and loads the full editor state to/from a JSON project file.
//
// What is saved:
//   - Camera transform (target, yaw, pitch, dist)
//   - Grammar settings (seed, min/max prim, hardcoded flag)
//   - Scene objects (transform, primId, mesh source path, color, sockets, gridCell)
//
// What is NOT saved (has its own persistence):
//   - Asset library (editor_assets.json, managed by AssetLibrary)
//
// Format: hand-written JSON — no external dependencies.

class ProjectFile
{
public:
    // Save current state. Returns true on success.
    static bool save(const std::string& path,
                     const Camera&      camera,
                     const GrammarView& grammar,
                     const Scene&       scene);

    // Load state into existing objects. Returns true on success.
    // Meshes referenced by sourcePath are re-imported from disk.
    // Grammar is NOT re-run — the saved scene objects are restored directly.
    static bool load(const std::string& path,
                     Camera&      camera,
                     GrammarView& grammar,
                     Scene&       scene,
                     MeshLibrary& meshLib);

    // Returns the last error string (empty if no error)
    static const std::string& lastError() { return s_error; }

private:
    static std::string s_error;

    // JSON helpers
    static std::string escapeJson(const std::string& s);
    static std::string vec3Json(const glm::vec3& v);
    static std::string vec2iJson(const glm::ivec2& v);
};

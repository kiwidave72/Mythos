#pragma once
#include "MeshAsset.h"
#include "../lib/grammar-core/Grammar.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>

// ---- WorldSocket -----------------------------------------------------------
// A connection point in world space.
// For grid-based grammar: worldPos derived from cell + socket gridDir.
// For free-form (Phase 8.5): worldPos and worldNorm set from mesh connector.
struct WorldSocket {
    glm::vec3    worldPos  = {0,0,0};
    glm::vec3    worldNorm = {1,0,0};   // direction the socket "faces"
    glm::ivec2   gridDir   = {0,0};     // kept for grid-mode grammar
    bool         connected = false;
    int          connectedTo = -1;      // index of partner SceneObject (-1 = free)
};

// ---- SceneObject -----------------------------------------------------------
// One object in the scene. Owns its transform, mesh reference, sockets.
// Does NOT know how it was created (grammar, manual, imported — doesn't matter).
struct SceneObject {
    // Identity
    int         id   = -1;
    std::string name;
    std::string primId;   // grammar prim type ("HStraight", etc.) or OBJ name

    // Transform (position, rotation, scale stored separately for editor use)
    glm::vec3   position = {0,0,0};
    glm::vec3   rotation = {0,0,0};   // Euler angles in degrees
    glm::vec3   scale    = {1,1,1};
    glm::mat4   transform() const;    // computed from pos/rot/scale

    // Mesh — shared, may be procedural cube or imported OBJ
    std::shared_ptr<MeshAsset> mesh;

    // Colour — used when no texture, or for procedural pieces
    glm::vec3   color = {0.8f, 0.8f, 0.8f};

    // Sockets (world-space connection points)
    std::vector<WorldSocket> sockets;

    // Grid cell (kept for grammar grid lookups; {0,0} if not grammar-placed)
    glm::ivec2  gridCell = {0,0};

    // State
    bool selected = false;
    bool hovered  = false;
    bool visible  = true;
};

inline glm::mat4 SceneObject::transform() const
{
    glm::mat4 m = glm::mat4(1.f);
    m = glm::translate(m, position);
    m = glm::rotate(m, glm::radians(rotation.y), {0,1,0});
    m = glm::rotate(m, glm::radians(rotation.x), {1,0,0});
    m = glm::rotate(m, glm::radians(rotation.z), {0,0,1});
    m = glm::scale(m, scale);
    return m;
}

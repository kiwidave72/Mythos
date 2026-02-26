#pragma once
// MerrellGraph — half-edge graph with abstract edge labels.
//
// Reference: Paul Merrell, "Example-Based Procedural Modeling Using Graph
// Grammars", ACM Trans. Graph. 42, 4, Article 1 (August 2023).
//
// DESIGN NOTES — READ BEFORE EDITING
// ====================================
// GRID-FIRST: theta values are float, currently multiples of pi/2 because
// we start with the tile grid. Do NOT store theta as int or enum.
// When free-form input arrives (MG-6), only the stored values change.
//
// COMPUTE SHADER: string labels (l, r) must be interned to int indices
// before GPU upload. Interning happens in MerrellGrammar — this struct
// stays string-based on the CPU side.
//
// No OpenGL, no ImGui, no GLFW dependency.

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cmath>

namespace merrell {

// pi as constexpr avoids all _USE_MATH_DEFINES / cmath ordering problems on MSVC.
static constexpr float MG_PI = 3.14159265358979323846f;

// ============================================================
// EdgeLabel
// ============================================================
// Abstract label on a directed half-edge.
// Sec 3.3: label ã = (l, r, theta) where:
//   l     = label from the left side (this half-edge's face)
//   r     = label from the right side (twin's face)
//   theta = tangent angle of the edge direction in radians [KEEP AS FLOAT]
//
// Grid labels:   "wall", "open", "exterior", or tile name
// Wildcard:      "" (empty) matches anything during rule application
struct EdgeLabel {
    std::string l;
    std::string r;
    float       theta = 0.f;  // radians — keep as float, see GRID-FIRST note above

    bool operator==(const EdgeLabel& o) const {
        return l == o.l && r == o.r && theta == o.theta;
    }
};

// ============================================================
// TurnType
// ============================================================
// Sec 3.3: turns between consecutive half-edges in a boundary string.
// Positive (^) = left turn  (CCW, +angle).
// Negative (v) = right turn (CW,  -angle).
enum class TurnType {
    Positive,   // ^ left turn  (convex corner traversing CCW)
    Negative,   // v right turn (concave corner traversing CCW)
};

// ============================================================
// BoundaryElement
// ============================================================
// One element in a boundary string — either an edge or a turn.
// Boundary strings alternate:  edge -> turn -> edge -> turn -> ...
struct BoundaryElement {
    bool      is_turn   = false;
    int       edge_id   = -1;
    TurnType  turn_type = TurnType::Positive;
};

// ============================================================
// BoundaryString
// ============================================================
// Circular sequence: a1 ^/v a2 ^/v ... an ^/v  (Sec 3.3)
// "complete" means |totalTurnCount()| == 4 (a closed polygon).
struct BoundaryString {
    std::vector<BoundaryElement> elements;

    bool isComplete() const;
    bool isEmpty()    const { return elements.empty(); }

    // Circular equality: ya^b == ^bya  (implemented MG-2)
    bool isCircularlyEqual(const BoundaryString& other) const;

    // Turn cancellation: a^x^v -> a^x  (implemented MG-2)
    void cancelTurns();

    // Return a copy rotated by `offset` positions (for matching).
    BoundaryString rotated(int offset) const;

    // +1 per positive turn, -1 per negative turn. Complete loops = +/-4.
    int totalTurnCount() const;

    // Human-readable for debug: "E0^E1^E2^E3^"
    std::string toString() const;
};

// ============================================================
// MGHalfEdge
// ============================================================
// One directed edge in the Merrell graph.
// NOT the same as HalfEdgeMesh::HalfEdge.
// twin == -1 during disassembly (MG-1); must be resolved before MG-3.
struct MGHalfEdge {
    int       id     = -1;
    int       twin   = -1;   // opposite half-edge (-1 = cut during disassembly)
    int       next   = -1;   // next half-edge around face (CCW)
    int       prev   = -1;   // prev half-edge around face (CCW)
    int       vertex = -1;   // start vertex of this directed edge
    int       face   = -1;   // face to the left (-1 = exterior/unset)
    EdgeLabel label;          // l, r, theta  [theta stays float — see note above]
};

// ============================================================
// MGVertex
// ============================================================
// pos is vec2 for grid-first phase.
// Promote to vec3 at MG-6 (3D extension).
struct MGVertex {
    int       id          = -1;
    glm::vec2 pos         = {0.f, 0.f};  // [promote to vec3 at MG-6]
    int       outgoing_he = -1;
};

// ============================================================
// MGFace
// ============================================================
// One face (region) in the Merrell graph.
// Grid phase: one face per tile primitive.
// Face label maps to a mesh asset in the Asset Library (MG-5).
struct MGFace {
    int         id       = -1;
    int         start_he = -1;
    std::string label;
    int         degree   = 0;   // boundary edge count, set by linkFaceLoop()
};

// ============================================================
// MerrellGraph
// ============================================================
// Central graph used by all Merrell algorithms.
// No grid cell coordinates — grid position is an editor concern.
class MerrellGraph {
public:
    std::vector<MGVertex>   vertices;
    std::vector<MGHalfEdge> halfEdges;
    std::vector<MGFace>     faces;

    // ---- Factory -----------------------------------------------------------

    // Add a vertex at pos. Returns its id.
    int addVertex(glm::vec2 pos);

    // Add a face with label. Returns face id.
    // Wire its half-edges with linkFaceLoop() after adding all edges.
    int addFace(const std::string& label);

    // Add a twin half-edge pair v0->v1 and v1->v0 with the given label.
    // Twin gets l/r swapped and theta += pi.
    // Returns the id of the v0->v1 half-edge.
    int addHalfEdgePair(int v0, int v1, const EdgeLabel& label);

    // Wire a list of half-edge ids into a CCW face loop.
    // Sets next, prev, face on each. Sets face.start_he and face.degree.
    void linkFaceLoop(int faceId, const std::vector<int>& heIds);

    // Reset all data and id counters.
    void clear();

    // ---- MG-2: Gluing helpers ----------------------------------------------

    // Identify vertex fromId with toId (weld two vertices into one).
    // Used during loop gluing when two graphs are merged at a shared boundary.
    void mergeVertices(int fromId, int toId);

    // Remove a half-edge pair by id. Patches next/prev of neighbours.
    // Used when a glued edge is internalised (no longer on the boundary).
    void removeHalfEdgePair(int heId);

    // ---- Accessors (linear scan — fine for small grammar graphs) -----------
    MGVertex*         vertex  (int id);
    MGHalfEdge*       halfEdge(int id);
    MGFace*           face    (int id);
    const MGVertex*   vertex  (int id) const;
    const MGHalfEdge* halfEdge(int id) const;
    const MGFace*     face    (int id) const;

    // ---- Boundary ----------------------------------------------------------
    // boundaryOf: implemented MG-1. outerBoundary: implemented MG-2.
    BoundaryString boundaryOf   (int faceId) const;
    BoundaryString outerBoundary()           const;

    // ---- Queries -----------------------------------------------------------
    bool isEmpty()     const { return vertices.empty(); }
    int  faceCount()   const { return (int)faces.size(); }
    int  edgeCount()   const { return (int)halfEdges.size() / 2; }
    int  vertexCount() const { return (int)vertices.size(); }

    // ---- Debug -------------------------------------------------------------
    void dump() const;

private:
    int m_nextVertexId   = 0;
    int m_nextHalfEdgeId = 0;
    int m_nextFaceId     = 0;
};

// ============================================================
// Utility: grid direction -> theta radians
// ============================================================
// GRID-FIRST ONLY: used exclusively in MerrellGrammar::loadFromTiles().
// Grid: E=(1,0) W=(-1,0) N=(0,-1) S=(0,1)  [Y increases downward]
// Math: E=0  N=pi/2  W=pi  S=3pi/2          [standard CCW from east]
// Flip Y so boundary strings are geometrically consistent in world space.
inline float gridDirToTheta(glm::ivec2 dir)
{
    if (dir.x ==  1 && dir.y ==  0) return 0.f;
    if (dir.x ==  0 && dir.y == -1) return MG_PI * 0.5f;
    if (dir.x == -1 && dir.y ==  0) return MG_PI;
    if (dir.x ==  0 && dir.y ==  1) return MG_PI * 1.5f;
    return 0.f;
}

} // namespace merrell

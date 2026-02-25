#pragma once
// HalfEdgeMesh — builds a half-edge structure from a MeshData triangle soup.
//
// Coordinate system: full 3D (glm::vec3). Faces are triangles from the source
// mesh — no polygon merging yet. That comes later once the basic split is working.
//
// No OpenGL, no ImGui, no GLFW dependency.

#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declare so we don't pull in all of MeshAsset.h from lib/
struct MeshData;
struct MeshVertex;

namespace grammar {

// ---------------------------------------------------------------------------
// HalfEdge
// ---------------------------------------------------------------------------
// One directed edge. Every mesh edge has exactly two half-edges (twins).
// Boundary edges (naked edges with no face on one side) have twin == -1.

struct HalfEdge {
    int id     = -1;
    int vertex = -1;   // start vertex index into HalfEdgeMesh::verts
    int twin   = -1;   // opposite directed edge (-1 = boundary / naked edge)
    int next   = -1;   // next HE around this face (CCW)
    int prev   = -1;   // prev HE around this face (CCW)
    int face   = -1;   // face this HE belongs to (-1 = boundary)

    // Semantic label — populated after build, used by grammar rules
    // "interior"  : shared between two faces (twin != -1)
    // "boundary"  : naked edge on the mesh perimeter (twin == -1)
    // User can override with "wall", "door", "window", etc.
    std::string label = "boundary";
};

// ---------------------------------------------------------------------------
// HEVertex
// ---------------------------------------------------------------------------

struct HEVertex {
    int       id          = -1;
    glm::vec3 pos         = {0,0,0};
    glm::vec3 normal      = {0,0,0};   // averaged from source mesh
    int       outgoingHe  = -1;        // any one outgoing HE (for fan traversal)
};

// ---------------------------------------------------------------------------
// HEFace
// ---------------------------------------------------------------------------
// Each face corresponds to one triangle from the source MeshData.

struct HEFace {
    int id      = -1;
    int startHe = -1;       // any HE of this face (walk via next to get all 3)

    glm::vec3 normal  = {0,0,0};   // face normal (computed on build)
    float     area    = 0.f;       // face area (computed on build)
    int       submesh = -1;        // which SubMesh this face came from (-1 = none)

    // Semantic label — "triangle" initially, grammar rules rename faces
    std::string label = "triangle";
};

// ---------------------------------------------------------------------------
// HalfEdgeMesh
// ---------------------------------------------------------------------------

struct BuildStats {
    int vertCount      = 0;
    int faceCount      = 0;
    int halfEdgeCount  = 0;
    int interiorEdges  = 0;   // twin != -1 (shared between two faces)
    int boundaryEdges  = 0;   // twin == -1 (naked / perimeter edges)
    int nonManifoldEdges = 0; // more than 2 faces sharing one edge (bad input)
    bool isManifold    = true;
};

class HalfEdgeMesh {
public:
    std::vector<HEVertex>  verts;
    std::vector<HalfEdge>  halfEdges;
    std::vector<HEFace>    faces;

    // ---- Build from MeshData -----------------------------------------------
    // Builds the half-edge structure from a triangle mesh.
    // Vertices are welded within epsilon world units before building.
    // Returns false if the input has no triangles.
    bool buildFromMesh(const MeshData& mesh, float weldEpsilon = 0.0001f);

    // ---- Stats & validation ------------------------------------------------
    BuildStats computeStats() const;

    // Check all structural invariants. Returns true if valid.
    // Writes a description of each violation to errors (if provided).
    bool validate(std::vector<std::string>* errors = nullptr) const;

    // ---- Traversal ---------------------------------------------------------
    std::vector<int> faceHalfEdges(int faceId) const;   // always 3 for triangles
    std::vector<int> faceVertices (int faceId) const;
    std::vector<int> vertexFaces  (int vertId) const;   // fan around vertex

    glm::vec3 faceNormal  (int faceId) const;
    glm::vec3 faceCentroid(int faceId) const;
    float     edgeLength  (int heId)   const;

    // ---- Debug -------------------------------------------------------------
    // Prints a full summary to stdout. Paste into chat for review.
    void dumpStats()  const;   // summary numbers only
    void dumpFaces(int maxFaces = 20) const;   // face list with labels
    void dumpEdges(int maxEdges = 30) const;   // edge list, boundary highlighted
    void dumpBoundaryLoops() const;            // walk each boundary loop
    void dumpNonManifold(int maxEdges = 10) const; // non-manifold edges with all sharing faces

private:
    // Internal build helpers
    void weldVertices(const MeshData& mesh, float epsilon,
                      std::vector<int>& outRemap,
                      std::vector<glm::vec3>& outPositions,
                      std::vector<glm::vec3>& outNormals);

    void buildTwins();   // after all faces added, find and link twin HEs
    void labelEdges();   // set "interior" / "boundary" labels
    void computeFaceData(); // normals and areas
};

} // namespace grammar

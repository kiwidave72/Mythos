#include "HalfEdgeMesh.h"
#include "../src/MeshAsset.h"   // MeshData, MeshVertex, SubMesh

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace grammar {

// ============================================================
// buildFromMesh
// ============================================================

bool HalfEdgeMesh::buildFromMesh(const MeshData& mesh, float weldEpsilon)
{
    verts.clear();
    halfEdges.clear();
    faces.clear();

    if (mesh.indices.empty() || mesh.vertices.empty()) {
        std::cout << "[HalfEdge] ERROR: empty mesh\n";
        return false;
    }
    if (mesh.indices.size() % 3 != 0) {
        std::cout << "[HalfEdge] ERROR: index count not divisible by 3 ("
                  << mesh.indices.size() << ")\n";
        return false;
    }

    // ------------------------------------------------------------------
    // Step 1: Weld vertices
    // ------------------------------------------------------------------
    std::vector<int>       remap;      // original vertex index -> welded index
    std::vector<glm::vec3> wPositions;
    std::vector<glm::vec3> wNormals;
    weldVertices(mesh, weldEpsilon, remap, wPositions, wNormals);

    // Build HEVertex list from welded positions
    verts.resize(wPositions.size());
    for (int i = 0; i < (int)wPositions.size(); ++i) {
        verts[i].id     = i;
        verts[i].pos    = wPositions[i];
        verts[i].normal = wNormals[i];
        verts[i].outgoingHe = -1;
    }

    // ------------------------------------------------------------------
    // Step 2: Remove duplicate faces (same 3 vertices, any winding order)
    // ------------------------------------------------------------------
    // After vertex welding, adjacent tiles that shared a wall now have
    // two triangles occupying exactly the same edge in world space --
    // the outer face of tile A and the inner face of tile B.
    // These appear as the same 3 welded vertex indices in either winding.
    // We keep only the first occurrence and drop all duplicates.
    //
    // Key: sorted (v0, v1, v2) so winding order does not matter.
    {
        std::map<std::tuple<int,int,int>, int> seen; // sorted key -> first tri index
        int totalTris      = (int)mesh.indices.size() / 3;
        int dupCount       = 0;
        int sameWinding    = 0;
        int flippedWinding = 0;

        std::vector<unsigned int> dedupIndices;
        dedupIndices.reserve(mesh.indices.size());

        for (int t = 0; t < totalTris; ++t) {
            int v0 = remap[mesh.indices[t * 3 + 0]];
            int v1 = remap[mesh.indices[t * 3 + 1]];
            int v2 = remap[mesh.indices[t * 3 + 2]];

            // Skip degenerates early
            if (v0 == v1 || v1 == v2 || v0 == v2) continue;

            // Canonical sorted key -- winding-agnostic
            int a = v0, b = v1, c = v2;
            if (a > b) std::swap(a, b);
            if (b > c) std::swap(b, c);
            if (a > b) std::swap(a, b);
            auto key = std::make_tuple(a, b, c);

            auto it = seen.find(key);
            if (it != seen.end()) {
                ++dupCount;
                // Distinguish same vs flipped winding for the log
                int fv0 = remap[mesh.indices[it->second * 3 + 0]];
                int fv1 = remap[mesh.indices[it->second * 3 + 1]];
                int fv2 = remap[mesh.indices[it->second * 3 + 2]];
                if (fv0 == v0 && fv1 == v1 && fv2 == v2) ++sameWinding;
                else                                       ++flippedWinding;
                continue; // drop
            }

            seen[key] = t;
            dedupIndices.push_back(mesh.indices[t * 3 + 0]);
            dedupIndices.push_back(mesh.indices[t * 3 + 1]);
            dedupIndices.push_back(mesh.indices[t * 3 + 2]);
        }

        std::cout << "[HalfEdge] Duplicate face removal: "
                  << totalTris << " -> "
                  << (int)(dedupIndices.size() / 3) << " tris ("
                  << dupCount << " removed: "
                  << sameWinding << " same-winding, "
                  << flippedWinding << " flipped-winding)\n";

        // ------------------------------------------------------------------
        // Step 3: Build faces and inner half-edges from deduplicated list
        // ------------------------------------------------------------------
        int triCount = (int)dedupIndices.size() / 3;
        faces.reserve(triCount);
        halfEdges.reserve(triCount * 3);

        for (int t = 0; t < triCount; ++t) {
            int v0 = remap[dedupIndices[t * 3 + 0]];
            int v1 = remap[dedupIndices[t * 3 + 1]];
            int v2 = remap[dedupIndices[t * 3 + 2]];
            // (degenerates already removed in dedup pass)

        // Create face
        HEFace f;
        f.id      = (int)faces.size();
        f.submesh = -1;   // submesh assignment deferred
        f.label   = "triangle";

        // Create 3 inner half-edges: he0 (v0->v1), he1 (v1->v2), he2 (v2->v0)
        int baseHe = (int)halfEdges.size();

        for (int e = 0; e < 3; ++e) {
            HalfEdge he;
            he.id   = baseHe + e;
            he.face = f.id;
            he.twin = -1;
            he.next = baseHe + (e + 1) % 3;
            he.prev = baseHe + (e + 2) % 3;
            halfEdges.push_back(he);
        }

        // Assign start vertices: he0 starts at v0, he1 at v1, he2 at v2
        halfEdges[baseHe + 0].vertex = v0;
        halfEdges[baseHe + 1].vertex = v1;
        halfEdges[baseHe + 2].vertex = v2;

        // Update vertex outgoing pointers (first one wins)
        for (int e = 0; e < 3; ++e) {
            int v = halfEdges[baseHe + e].vertex;
            if (verts[v].outgoingHe == -1)
                verts[v].outgoingHe = baseHe + e;
        }

        f.startHe = baseHe;
        faces.push_back(f);
    }
    } // end dedup scope

    if (faces.empty()) {
        std::cout << "[HalfEdge] ERROR: no valid (non-degenerate) triangles\n";
        return false;
    }

    // ------------------------------------------------------------------
    // Step 4: Find and link twins
    // ------------------------------------------------------------------
    buildTwins();

    // ------------------------------------------------------------------
    // Step 5: Label edges and compute face data
    // ------------------------------------------------------------------
    labelEdges();
    computeFaceData();

    std::cout << "[HalfEdge] Built from mesh: "
              << verts.size()     << " verts, "
              << faces.size()     << " faces, "
              << halfEdges.size() << " half-edges\n";

    return true;
}

// ============================================================
// weldVertices
// ============================================================
// Collapse vertices within epsilon using a grid-bucketed spatial hash.
// remap[i] = welded vertex index for original vertex i.

void HalfEdgeMesh::weldVertices(const MeshData& mesh, float epsilon,
                                  std::vector<int>& remap,
                                  std::vector<glm::vec3>& outPositions,
                                  std::vector<glm::vec3>& outNormals)
{
    int n = (int)mesh.vertices.size();
    remap.resize(n, -1);

    // Bucket vertices into grid cells of size epsilon
    struct GridKey {
        int x, y, z;
        bool operator==(const GridKey& o) const {
            return x==o.x && y==o.y && z==o.z;
        }
    };
    struct GridHash {
        size_t operator()(const GridKey& k) const {
            size_t h = (size_t)k.x * 73856093u
                     ^ (size_t)k.y * 19349669u
                     ^ (size_t)k.z * 83492791u;
            return h;
        }
    };

    auto toKey = [&](const glm::vec3& p) -> GridKey {
        return {
            (int)std::floor(p.x / epsilon),
            (int)std::floor(p.y / epsilon),
            (int)std::floor(p.z / epsilon)
        };
    };

    std::unordered_map<GridKey, std::vector<int>, GridHash> grid;

    for (int i = 0; i < n; ++i) {
        const glm::vec3& p = mesh.vertices[i].pos;
        GridKey key = toKey(p);

        // Check this cell and all 26 neighbours
        bool found = false;
        for (int dx = -1; dx <= 1 && !found; ++dx)
        for (int dy = -1; dy <= 1 && !found; ++dy)
        for (int dz = -1; dz <= 1 && !found; ++dz) {
            GridKey nk = {key.x+dx, key.y+dy, key.z+dz};
            auto it = grid.find(nk);
            if (it == grid.end()) continue;
            for (int j : it->second) {
                const glm::vec3& q = outPositions[j];
                float dx2 = p.x-q.x, dy2 = p.y-q.y, dz2 = p.z-q.z;
                if (dx2*dx2 + dy2*dy2 + dz2*dz2 <= epsilon*epsilon) {
                    remap[i] = j;
                    // Accumulate normals for averaging
                    outNormals[j] += mesh.vertices[i].normal;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            remap[i] = (int)outPositions.size();
            outPositions.push_back(p);
            outNormals.push_back(mesh.vertices[i].normal);
            grid[key].push_back(remap[i]);
        }
    }

    // Normalise accumulated normals
    for (auto& n : outNormals) {
        float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
        if (len > 1e-6f) n = n * (1.f / len);
    }

    int welded = n - (int)outPositions.size();
    std::cout << "[HalfEdge] Vertex weld: " << n << " -> "
              << outPositions.size() << " ("
              << welded << " collapsed)\n";
}

// ============================================================
// buildTwins
// ============================================================
// For every half-edge (u->v), find the half-edge (v->u) in another face.
// Use a map keyed on (u,v) edge pair.

void HalfEdgeMesh::buildTwins()
{
    // Map from directed edge (u->v) -> list of all half-edge ids with that direction.
    // For a clean manifold mesh every entry has exactly 1 element.
    // Non-manifold edges have 2+ elements — we leave those unlinked (twin = -1)
    // rather than making broken symmetric links.
    std::map<std::pair<int,int>, std::vector<int>> edgeMap;

    for (auto& he : halfEdges) {
        int u = he.vertex;
        int v = halfEdges[he.next].vertex;
        edgeMap[{u, v}].push_back(he.id);
    }

    // Count non-manifold directed edges (more than one HE in the same direction)
    int nonManifold = 0;
    for (auto& kv : edgeMap)
        if ((int)kv.second.size() > 1) nonManifold += (int)kv.second.size();

    // Link twins: for each (u->v), look for exactly one (v->u).
    // If there are multiple (v->u) candidates the edge is non-manifold — skip it.
    int twinned = 0;
    for (auto& he : halfEdges) {
        if (he.twin != -1) continue;

        int u = he.vertex;
        int v = halfEdges[he.next].vertex;

        auto it = edgeMap.find({v, u});
        if (it == edgeMap.end()) continue;               // no candidate at all
        if ((int)it->second.size() != 1) continue;      // non-manifold — skip
        int candidateId = it->second[0];
        if (candidateId == he.id) continue;              // self-reference guard

        // Also check that the candidate's own direction isn't duplicated
        auto selfIt = edgeMap.find({u, v});
        if (selfIt != edgeMap.end() && (int)selfIt->second.size() != 1) continue;

        he.twin = candidateId;
        halfEdges[candidateId].twin = he.id;
        ++twinned;
    }

    if (nonManifold > 0)
        std::cout << "[HalfEdge] WARNING: " << nonManifold
                  << " non-manifold directed edges (left unlinked)\n";

    std::cout << "[HalfEdge] Twin linking: "
              << twinned << " pairs linked\n";
}

// ============================================================
// labelEdges
// ============================================================

void HalfEdgeMesh::labelEdges()
{
    int interior = 0, boundary = 0;
    for (auto& he : halfEdges) {
        if (he.twin != -1) {
            he.label = "interior";
            ++interior;
        } else {
            he.label = "boundary";
            ++boundary;
        }
    }
    std::cout << "[HalfEdge] Edge labels: "
              << interior << " interior, "
              << boundary << " boundary\n";
}

// ============================================================
// computeFaceData
// ============================================================

void HalfEdgeMesh::computeFaceData()
{
    for (auto& f : faces) {
        auto hes = faceHalfEdges(f.id);
        assert(hes.size() == 3);

        const glm::vec3& a = verts[halfEdges[hes[0]].vertex].pos;
        const glm::vec3& b = verts[halfEdges[hes[1]].vertex].pos;
        const glm::vec3& c = verts[halfEdges[hes[2]].vertex].pos;

        glm::vec3 ab = b - a;
        glm::vec3 ac = c - a;

        // Cross product for normal and area
        glm::vec3 cross = {
            ab.y*ac.z - ab.z*ac.y,
            ab.z*ac.x - ab.x*ac.z,
            ab.x*ac.y - ab.y*ac.x
        };
        float len = std::sqrt(cross.x*cross.x + cross.y*cross.y + cross.z*cross.z);
        f.area   = len * 0.5f;
        f.normal = (len > 1e-8f) ? cross * (1.f / len) : glm::vec3{0,1,0};
    }
}

// ============================================================
// Traversal
// ============================================================

std::vector<int> HalfEdgeMesh::faceHalfEdges(int faceId) const
{
    std::vector<int> result;
    int start = faces[faceId].startHe;
    int cur   = start;
    int guard = 0;
    do {
        result.push_back(cur);
        cur = halfEdges[cur].next;
        if (++guard > (int)halfEdges.size()) break;
    } while (cur != start);
    return result;
}

std::vector<int> HalfEdgeMesh::faceVertices(int faceId) const
{
    auto hes = faceHalfEdges(faceId);
    std::vector<int> result;
    for (int h : hes) result.push_back(halfEdges[h].vertex);
    return result;
}

std::vector<int> HalfEdgeMesh::vertexFaces(int vertId) const
{
    std::vector<int> result;
    int start = verts[vertId].outgoingHe;
    if (start == -1) return result;
    int cur = start;
    int guard = 0;
    do {
        if (halfEdges[cur].face != -1)
            result.push_back(halfEdges[cur].face);
        int t = halfEdges[cur].twin;
        if (t == -1) break;
        cur = halfEdges[t].next;
        if (++guard > (int)halfEdges.size()) break;
    } while (cur != start);
    return result;
}

glm::vec3 HalfEdgeMesh::faceNormal(int faceId) const
{
    return faces[faceId].normal;
}

glm::vec3 HalfEdgeMesh::faceCentroid(int faceId) const
{
    auto vids = faceVertices(faceId);
    glm::vec3 sum = {0,0,0};
    for (int v : vids) sum = sum + verts[v].pos;
    float inv = 1.f / (float)vids.size();
    return sum * inv;
}

float HalfEdgeMesh::edgeLength(int heId) const
{
    int u = halfEdges[heId].vertex;
    int v = halfEdges[halfEdges[heId].next].vertex;
    glm::vec3 d = verts[v].pos - verts[u].pos;
    return std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
}

// ============================================================
// computeStats
// ============================================================

BuildStats HalfEdgeMesh::computeStats() const
{
    BuildStats s;
    s.vertCount     = (int)verts.size();
    s.faceCount     = (int)faces.size();
    s.halfEdgeCount = (int)halfEdges.size();

    for (const auto& he : halfEdges) {
        if (he.face == -1) continue;  // skip boundary stubs
        if (he.twin != -1) ++s.interiorEdges;
        else               ++s.boundaryEdges;
    }
    // Each interior edge is counted twice (once per direction) so halve it
    s.interiorEdges /= 2;

    // Non-manifold check: any vertex whose outgoing fan doesn't close
    // (can only detect after twin linking)
    for (const auto& v : verts) {
        if (v.outgoingHe == -1) { s.isManifold = false; continue; }
        // Simple check: walk fan, if we hit a boundary it's not a closed fan
        // That's expected at boundary vertices, not a manifold error per se
    }

    // Better non-manifold check: any directed edge appears more than once
    std::map<std::pair<int,int>, int> seen;
    for (const auto& he : halfEdges) {
        if (he.face == -1) continue;
        int u = he.vertex;
        int v = halfEdges[he.next].vertex;
        seen[{u,v}]++;
    }
    for (auto& kv : seen) {
        if (kv.second > 1) {
            ++s.nonManifoldEdges;
            s.isManifold = false;
        }
    }

    return s;
}

// ============================================================
// validate
// ============================================================

bool HalfEdgeMesh::validate(std::vector<std::string>* errors) const
{
    bool ok = true;
    auto err = [&](const std::string& msg) {
        ok = false;
        if (errors) errors->push_back(msg);
        else std::cout << "  [INVALID] " << msg << "\n";
    };

    for (const auto& he : halfEdges) {
        if (he.next < 0 || he.next >= (int)halfEdges.size())
            err("he" + std::to_string(he.id) + " invalid next=" + std::to_string(he.next));
        else if (halfEdges[he.next].prev != he.id)
            err("he" + std::to_string(he.id) + " next/prev mismatch");

        if (he.prev < 0 || he.prev >= (int)halfEdges.size())
            err("he" + std::to_string(he.id) + " invalid prev=" + std::to_string(he.prev));

        if (he.twin != -1) {
            if (he.twin < 0 || he.twin >= (int)halfEdges.size())
                err("he" + std::to_string(he.id) + " invalid twin=" + std::to_string(he.twin));
            else if (halfEdges[he.twin].twin != he.id)
                err("he" + std::to_string(he.id) + " twin not symmetric");
        }

        if (he.vertex < 0 || he.vertex >= (int)verts.size())
            err("he" + std::to_string(he.id) + " invalid vertex=" + std::to_string(he.vertex));

        if (he.face != -1 && (he.face < 0 || he.face >= (int)faces.size()))
            err("he" + std::to_string(he.id) + " invalid face=" + std::to_string(he.face));
    }

    for (const auto& f : faces) {
        auto hes = faceHalfEdges(f.id);
        if (hes.size() != 3)
            err("face" + std::to_string(f.id) + " has " +
                std::to_string(hes.size()) + " edges (expected 3)");
        for (int h : hes)
            if (halfEdges[h].face != f.id)
                err("face" + std::to_string(f.id) + " he" + std::to_string(h) +
                    " points to wrong face " + std::to_string(halfEdges[h].face));
    }

    return ok;
}

// ============================================================
// dumpStats
// ============================================================

void HalfEdgeMesh::dumpStats() const
{
    BuildStats s = computeStats();
    std::cout << "\n===== HalfEdgeMesh Stats =====\n";
    std::cout << "  Vertices     : " << s.vertCount     << "\n";
    std::cout << "  Faces        : " << s.faceCount     << " (triangles)\n";
    std::cout << "  Half-edges   : " << s.halfEdgeCount << "\n";
    std::cout << "  Interior edges (shared) : " << s.interiorEdges << "\n";
    std::cout << "  Boundary edges (naked)  : " << s.boundaryEdges << "\n";
    std::cout << "  Non-manifold edges      : " << s.nonManifoldEdges << "\n";
    std::cout << "  Manifold     : " << (s.isManifold ? "YES" : "NO") << "\n";

    // Euler characteristic: V - E + F = 2 for a closed manifold sphere
    // E = (interiorEdges + boundaryEdges)
    int E = s.interiorEdges + s.boundaryEdges;
    int chi = s.vertCount - E + s.faceCount;
    std::cout << "  Euler char (V-E+F) : " << chi
              << "  (2=sphere/closed, 1=disk, other=complex)\n";

    // Boundary loop count
    int boundaryLoops = 0;
    std::vector<bool> visited(halfEdges.size(), false);
    for (const auto& he : halfEdges) {
        if (he.twin != -1 || visited[he.id]) continue;
        ++boundaryLoops;
        int cur = he.id;
        int guard = 0;
        do {
            visited[cur] = true;
            // Walk boundary loop: find next boundary he from end vertex
            int endV = halfEdges[halfEdges[cur].next].vertex;
            // Search for another boundary he starting from endV
            // (this is O(n) per loop but we only run this for debug)
            int next = -1;
            for (const auto& other : halfEdges) {
                if (other.twin == -1 && !visited[other.id] &&
                    other.vertex == endV) {
                    next = other.id; break;
                }
            }
            if (next == -1) break;
            cur = next;
            if (++guard > (int)halfEdges.size()) break;
        } while (cur != he.id);
    }
    std::cout << "  Boundary loops : " << boundaryLoops << "\n";
    std::cout << "==============================\n\n";
}

// ============================================================
// dumpFaces
// ============================================================

void HalfEdgeMesh::dumpFaces(int maxFaces) const
{
    std::cout << "\n===== Faces (first " << maxFaces << " of "
              << faces.size() << ") =====\n";
    int shown = 0;
    for (const auto& f : faces) {
        if (shown++ >= maxFaces) { std::cout << "  ...\n"; break; }
        auto vids = faceVertices(f.id);
        std::cout << "  f" << f.id
                  << "  [" << f.label << "]"
                  << "  area=" << f.area
                  << "  norm=(" << f.normal.x << ","
                                << f.normal.y << ","
                                << f.normal.z << ")"
                  << "  verts=";
        for (int v : vids) {
            const glm::vec3& p = verts[v].pos;
            std::cout << "v" << v << "(" << p.x << "," << p.y << "," << p.z << ") ";
        }
        std::cout << "\n";
    }
    std::cout << "==============================\n\n";
}

// ============================================================
// dumpEdges
// ============================================================

void HalfEdgeMesh::dumpEdges(int maxEdges) const
{
    std::cout << "\n===== Half-edges (first " << maxEdges << " of "
              << halfEdges.size() << ") =====\n";
    int shown = 0;
    for (const auto& he : halfEdges) {
        if (shown++ >= maxEdges) { std::cout << "  ...\n"; break; }
        int endV = halfEdges[he.next].vertex;
        std::cout << "  he" << he.id
                  << "  v" << he.vertex << "->v" << endV
                  << "  twin=" << he.twin
                  << "  face=" << he.face
                  << "  [" << he.label << "]";
        if (he.twin == -1) std::cout << "  *** BOUNDARY ***";
        std::cout << "\n";
    }
    std::cout << "==============================\n\n";
}

// ============================================================
// dumpBoundaryLoops
// ============================================================

void HalfEdgeMesh::dumpBoundaryLoops() const
{
    std::cout << "\n===== Boundary Loops =====\n";

    // Collect all boundary HEs (twin == -1)
    std::vector<int> boundaryHEs;
    for (const auto& he : halfEdges)
        if (he.twin == -1) boundaryHEs.push_back(he.id);

    if (boundaryHEs.empty()) {
        std::cout << "  None — mesh is closed (no boundary edges)\n";
        std::cout << "==========================\n\n";
        return;
    }

    // Walk each boundary loop by following: from end vertex of he,
    // find the next boundary he starting at that vertex.
    // Build adjacency for boundary HEs: endVert -> heId
    std::map<int, std::vector<int>> boundaryFromVert;
    for (int hid : boundaryHEs) {
        int endV = halfEdges[halfEdges[hid].next].vertex;
        // The boundary loop goes against the face winding, so the
        // "next boundary he" starts at the end vertex of this one.
        // Actually: for a boundary edge, the loop direction is
        // found by going he.next.next... but those are face edges.
        // Correct approach: the boundary loops are formed by the
        // half-edges themselves via their end vertices.
        boundaryFromVert[endV].push_back(hid);
    }

    std::vector<bool> visited(halfEdges.size(), false);
    int loopIdx = 0;

    for (int startHe : boundaryHEs) {
        if (visited[startHe]) continue;
        ++loopIdx;
        std::cout << "  Loop " << loopIdx << ": ";

        // Walk the boundary loop
        // A boundary loop is formed by the inner HEs whose twins are boundary HEs.
        // From a boundary HE b (v->u), the next boundary HE is found by:
        // going to u's fan of inner HEs and finding the one whose twin is also boundary.
        // Simpler: just follow the inner face loop backwards around the boundary.
        //
        // The canonical traversal: boundary HE b covers edge u->v (in face winding
        // reversed direction). Next boundary HE is the one that starts at v
        // and also has no twin.
        //
        // Specifically: b.next in the face goes clockwise around the boundary hole.
        // We want counter-clockwise. So we go: twin of b.prev in the face.
        // But b has no twin. So: find the face HE pointing INTO the same end vertex
        // from an adjacent boundary position.
        //
        // Simplest correct method: precompute map from startVertex -> boundary HE
        std::map<int, int> bStart;
        for (int hid : boundaryHEs) bStart[halfEdges[hid].vertex] = hid;

        std::vector<int> loop;
        int cur = startHe;
        int guard = 0;
        do {
            if (visited[cur]) break;
            visited[cur] = true;
            loop.push_back(cur);
            // Next boundary HE: starts at the end vertex of this one
            int endV = halfEdges[halfEdges[cur].next].vertex;
            auto it = bStart.find(endV);
            if (it == bStart.end()) break;
            cur = it->second;
            if (++guard > (int)halfEdges.size()) break;
        } while (cur != startHe);

        for (int hid : loop)
            std::cout << "v" << halfEdges[hid].vertex << " ";
        std::cout << "  (" << loop.size() << " edges)\n";
    }

    std::cout << "  Total boundary HEs: " << boundaryHEs.size() << "\n";
    std::cout << "==========================\n\n";
}

// ============================================================
// dumpNonManifold
// ============================================================
// Finds edges where more than 2 faces share the same undirected edge (u,v).
// Prints the vertex positions and every face that touches that edge so you
// can see exactly what geometry is colliding.

void HalfEdgeMesh::dumpNonManifold(int maxEdges) const
{
    std::cout << "\n===== Non-Manifold Edges (first " << maxEdges << ") =====\n";

    // Build a map: canonical edge (min,max) -> list of half-edge ids
    // We use (min,max) so both directions of the same edge bucket together.
    std::map<std::pair<int,int>, std::vector<int>> edgeFaces;

    for (const auto& he : halfEdges) {
        if (he.face == -1) continue;
        int u = he.vertex;
        int v = halfEdges[he.next].vertex;
        auto key = std::make_pair(std::min(u,v), std::max(u,v));
        edgeFaces[key].push_back(he.id);
    }

    int found = 0;
    int totalNonManifold = 0;

    for (const auto& kv : edgeFaces) {
        if (kv.second.size() <= 2) continue;  // manifold — skip
        ++totalNonManifold;
        if (found >= maxEdges) continue;
        ++found;

        int u = kv.first.first;
        int v = kv.first.second;
        const glm::vec3& pu = verts[u].pos;
        const glm::vec3& pv = verts[v].pos;

        std::cout << "\n  Edge v" << u << " <-> v" << v
                  << "  (" << kv.second.size() << " faces share this edge)\n";
        std::cout << "    v" << u << " pos: ("
                  << pu.x << ", " << pu.y << ", " << pu.z << ")\n";
        std::cout << "    v" << v << " pos: ("
                  << pv.x << ", " << pv.y << ", " << pv.z << ")\n";
        std::cout << "    Edge length: " << edgeLength(kv.second[0]) << "\n";
        std::cout << "    Sharing faces:\n";

        for (int heId : kv.second) {
            int fid = halfEdges[heId].face;
            const HEFace& f = faces[fid];
            auto vids = faceVertices(fid);

            std::cout << "      f" << fid
                      << "  norm=(" << f.normal.x << ","
                                    << f.normal.y << ","
                                    << f.normal.z << ")"
                      << "  area=" << f.area << "\n";
            std::cout << "        verts: ";
            for (int vid : vids) {
                const glm::vec3& p = verts[vid].pos;
                std::cout << "v" << vid
                          << "(" << p.x << "," << p.y << "," << p.z << ") ";
            }
            std::cout << "\n";
        }

        // Diagnosis hint
        // Check if all faces have the same normal — if so it's duplicate/overlapping geometry
        glm::vec3 n0 = faces[halfEdges[kv.second[0]].face].normal;
        bool sameNormal = true;
        for (int i = 1; i < (int)kv.second.size(); ++i) {
            glm::vec3 ni = faces[halfEdges[kv.second[i]].face].normal;
            float dot = n0.x*ni.x + n0.y*ni.y + n0.z*ni.z;
            if (dot < 0.99f) { sameNormal = false; break; }
        }
        if (sameNormal) {
            std::cout << "    >> DIAGNOSIS: All faces same normal — likely duplicate/overlapping geometry\n";
        } else {
            std::cout << "    >> DIAGNOSIS: Faces have different normals — likely T-junction or fan at this edge\n";
        }
    }

    std::cout << "\n  Total non-manifold edges: " << totalNonManifold << "\n";
    std::cout << "==========================================\n\n";
}

} // namespace grammar

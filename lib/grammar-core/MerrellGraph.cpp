#include "MerrellGraph.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <unordered_map>
#include <set>

namespace merrell {

// ============================================================
// BoundaryString — operations
// ============================================================

int BoundaryString::totalTurnCount() const
{
    int count = 0;
    for (const auto& e : elements)
        if (e.is_turn)
            count += (e.turn_type == TurnType::Positive) ? +1 : -1;
    return count;
}

bool BoundaryString::isComplete() const
{
    return std::abs(totalTurnCount()) == 4;
}

// Circular equality: two boundary strings are equal if one is a cyclic rotation
// of the other (Sec 3.3 "ya∧b = ∧bya").
// We compare element-by-element after trying every rotation offset.
bool BoundaryString::isCircularlyEqual(const BoundaryString& other) const
{
    if (elements.size() != other.elements.size()) return false;
    if (elements.empty()) return true;

    int n = (int)elements.size();

    auto elemEqual = [](const BoundaryElement& a, const BoundaryElement& b) -> bool {
        if (a.is_turn != b.is_turn) return false;
        if (a.is_turn)  return a.turn_type == b.turn_type;
        // For edges we compare the edge id — callers must ensure ids are
        // normalised (remapped) before calling this if graphs have been merged.
        return a.edge_id == b.edge_id;
    };

    // Try every rotation of *this and check if it matches other.
    // O(n²) but boundary strings are short (typically 4-16 elements).
    for (int offset = 0; offset < n; ++offset) {
        bool match = true;
        for (int i = 0; i < n; ++i) {
            if (!elemEqual(elements[(i + offset) % n], other.elements[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Turn cancellation: remove adjacent Positive/Negative pairs (and vice versa).
// Sec 4.2: a∧x∧v = a∧x  (consecutive turns of opposite sign cancel).
// Repeated until no more cancellations are possible (convergence).
void BoundaryString::cancelTurns()
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i + 1 < (int)elements.size(); ) {
            auto& a = elements[i];
            auto& b = elements[i + 1];
            if (a.is_turn && b.is_turn && a.turn_type != b.turn_type) {
                // Adjacent opposite turns cancel
                elements.erase(elements.begin() + i, elements.begin() + i + 2);
                changed = true;
                // Don't advance i — check the new pair at position i
            } else {
                ++i;
            }
        }
        // Also check wrap-around (circular): last and first element
        if (elements.size() >= 2) {
            auto& last  = elements.back();
            auto& first = elements.front();
            if (last.is_turn && first.is_turn && last.turn_type != first.turn_type) {
                elements.erase(elements.begin());             // remove first
                elements.erase(elements.begin() + (int)elements.size() - 1); // remove last
                changed = true;
            }
        }
    }
}

// Returns a new BoundaryString that is this string rotated so it starts at
// element index `offset`. Used during hierarchy matching (MG-3).
BoundaryString BoundaryString::rotated(int offset) const
{
    if (elements.empty()) return {};
    int n = (int)elements.size();
    BoundaryString result;
    result.elements.reserve(n);
    for (int i = 0; i < n; ++i)
        result.elements.push_back(elements[(i + offset) % n]);
    return result;
}

std::string BoundaryString::toString() const
{
    std::ostringstream ss;
    for (const auto& e : elements) {
        if (e.is_turn) {
            ss << (e.turn_type == TurnType::Positive ? "^" : "v");
        } else {
            // Show edge type: O=open, X=exterior, G=glued, ?=unknown
            char typeChar = '?';
            if      (e.edge_label == "open")     typeChar = 'O';
            else if (e.edge_label == "exterior")  typeChar = 'X';
            else if (e.edge_label == "glued")     typeChar = 'G';
            ss << typeChar << e.edge_id;
        }
    }
    return ss.str();
}

// ============================================================
// MerrellGraph — factory
// ============================================================

void MerrellGraph::clear()
{
    vertices.clear();
    halfEdges.clear();
    faces.clear();
    m_nextVertexId   = 0;
    m_nextHalfEdgeId = 0;
    m_nextFaceId     = 0;
}

int MerrellGraph::addVertex(glm::vec2 pos)
{
    MGVertex v;
    v.id  = m_nextVertexId++;
    v.pos = pos;
    vertices.push_back(v);
    return v.id;
}

int MerrellGraph::addFace(const std::string& label)
{
    MGFace f;
    f.id    = m_nextFaceId++;
    f.label = label;
    faces.push_back(f);
    return f.id;
}

int MerrellGraph::addHalfEdgePair(int v0, int v1, const EdgeLabel& label)
{
    // Forward: v0 -> v1
    MGHalfEdge he;
    he.id     = m_nextHalfEdgeId++;
    he.vertex = v0;
    he.label  = label;
    int heId  = he.id;

    // Backward twin: v1 -> v0, l/r swapped, theta + pi
    EdgeLabel twinLabel;
    twinLabel.l     = label.r;
    twinLabel.r     = label.l;
    twinLabel.theta = label.theta + MG_PI;
    if (twinLabel.theta >= 2.f * MG_PI)
        twinLabel.theta -= 2.f * MG_PI;

    MGHalfEdge twin;
    twin.id     = m_nextHalfEdgeId++;
    twin.vertex = v1;
    twin.label  = twinLabel;
    int twinId  = twin.id;

    he.twin   = twinId;
    twin.twin = heId;

    halfEdges.push_back(he);
    halfEdges.push_back(twin);
    return heId;
}

void MerrellGraph::linkFaceLoop(int faceId, const std::vector<int>& heIds)
{
    if (heIds.empty()) return;
    MGFace* f = face(faceId);
    if (!f) return;

    int n = (int)heIds.size();
    for (int i = 0; i < n; ++i) {
        MGHalfEdge* he  = halfEdge(heIds[i]);
        MGHalfEdge* nxt = halfEdge(heIds[(i + 1) % n]);
        MGHalfEdge* prv = halfEdge(heIds[(i + n - 1) % n]);
        if (!he || !nxt || !prv) continue;
        he->next = nxt->id;
        he->prev = prv->id;
        he->face = faceId;
    }

    f->start_he = heIds[0];
    f->degree   = n;

    for (int heId : heIds) {
        MGHalfEdge* he = halfEdge(heId);
        if (!he) continue;
        MGVertex* v = vertex(he->vertex);
        if (v && v->outgoing_he == -1)
            v->outgoing_he = heId;
    }
}

// ============================================================
// MerrellGraph — gluing (MG-2)
// ============================================================
//
// mergeVertices: identify vertex `fromId` with `toId`.
// Moves all half-edges that start at `fromId` to start at `toId`.
// Removes the `fromId` vertex. Called during loop gluing to weld vertices.
void MerrellGraph::mergeVertices(int fromId, int toId)
{
    if (fromId == toId) return;
    for (auto& he : halfEdges)
        if (he.vertex == fromId) he.vertex = toId;
    // Remove the fromId vertex
    vertices.erase(
        std::remove_if(vertices.begin(), vertices.end(),
            [fromId](const MGVertex& v){ return v.id == fromId; }),
        vertices.end());
}

// removeHalfEdgePair: remove the half-edge with the given id and its twin.
// Used when two cut edges are glued (they become interior and disappear from
// the boundary). The face pointers of surrounding half-edges remain valid.
//
// After removal the next/prev chain may be broken — caller must re-link
// if the face loop needs to be traversable. For MG-2 we re-link via
// outerBoundary() recomputation rather than maintaining the chain.
void MerrellGraph::removeHalfEdgePair(int heId)
{
    const MGHalfEdge* he = halfEdge(heId);
    if (!he) return;
    int twinId = he->twin;

    // Patch next/prev of neighbours to skip these two half-edges
    auto patch = [&](int id) {
        MGHalfEdge* h = halfEdge(id);
        if (!h) return;
        int prevId = h->prev;
        int nextId = h->next;
        MGHalfEdge* prev = (prevId >= 0) ? halfEdge(prevId) : nullptr;
        MGHalfEdge* next = (nextId >= 0) ? halfEdge(nextId) : nullptr;
        // Stitch around: prev->next skips h, next->prev skips h
        if (prev && prev->next == id) prev->next = nextId;
        if (next && next->prev == id) next->prev = prevId;
        // Fix face start_he if it pointed to this edge
        for (auto& f : faces) {
            if (f.start_he == id)
                f.start_he = (nextId != id) ? nextId : -1;
        }
    };
    patch(heId);
    if (twinId != -1) patch(twinId);

    halfEdges.erase(
        std::remove_if(halfEdges.begin(), halfEdges.end(),
            [heId, twinId](const MGHalfEdge& h){
                return h.id == heId || h.id == twinId;
            }),
        halfEdges.end());
}

// ============================================================
// MerrellGraph — accessors
// ============================================================

MGVertex*         MerrellGraph::vertex  (int id)       { for (auto& v : vertices)  if (v.id  == id) return &v;  return nullptr; }
MGHalfEdge*       MerrellGraph::halfEdge(int id)       { for (auto& he: halfEdges) if (he.id == id) return &he; return nullptr; }
MGFace*           MerrellGraph::face    (int id)       { for (auto& f : faces)     if (f.id  == id) return &f;  return nullptr; }
const MGVertex*   MerrellGraph::vertex  (int id) const { for (const auto& v : vertices)  if (v.id  == id) return &v;  return nullptr; }
const MGHalfEdge* MerrellGraph::halfEdge(int id) const { for (const auto& he: halfEdges) if (he.id == id) return &he; return nullptr; }
const MGFace*     MerrellGraph::face    (int id) const { for (const auto& f : faces)     if (f.id  == id) return &f;  return nullptr; }

// ============================================================
// MerrellGraph — boundary (MG-1 / MG-2)
// ============================================================

BoundaryString MerrellGraph::boundaryOf(int faceId) const
{
    const MGFace* f = face(faceId);
    if (!f || f->start_he == -1) return {};

    std::vector<const MGHalfEdge*> loop;
    {
        int cur    = f->start_he;
        int safety = 0;
        do {
            const MGHalfEdge* he = halfEdge(cur);
            if (!he || ++safety > 1000) break;
            loop.push_back(he);
            cur = he->next;
        } while (cur != f->start_he);
    }
    if (loop.empty()) return {};

    BoundaryString bs;
    int n = (int)loop.size();
    for (int i = 0; i < n; ++i) {
        BoundaryElement edgeElem;
        edgeElem.is_turn    = false;
        edgeElem.edge_id    = loop[i]->id;
        edgeElem.edge_label = loop[i]->label.r;
        bs.elements.push_back(edgeElem);

        float t0 = loop[i]->label.theta;
        float t1 = loop[(i + 1) % n]->label.theta;
        float cross = std::cos(t0) * std::sin(t1) - std::sin(t0) * std::cos(t1);

        if (std::abs(cross) > 1e-5f) {
            BoundaryElement turnElem;
            turnElem.is_turn   = true;
            turnElem.turn_type = (cross > 0.f) ? TurnType::Positive : TurnType::Negative;
            bs.elements.push_back(turnElem);
        }
    }
    return bs;
}

BoundaryString MerrellGraph::outerBoundary() const
{
    // Outer boundary extraction for a planar half-edge graph.
    //
    // A half-edge belongs to the outer boundary iff its twin is exterior
    // (twin->face == -1, meaning the twin was never assigned to a face).
    //
    // Algorithm:
    //   1. Collect all boundary half-edge ids.
    //   2. Build: endVertex -> list of boundary he ids STARTING at that vertex.
    //      (Using a multimap since after merging, two boundary edges can start
    //       at the same vertex — one from each incident face.)
    //   3. Walk starting from boundaryIds[0]:
    //      a. Record current edge.
    //      b. Compute end vertex.
    //      c. Among all boundary edges starting at end vertex, pick the one
    //         that is NOT the reverse of current (i.e. not he->twin) AND
    //         has not been visited.
    //      d. Insert turn between current and next.
    //      e. Repeat until back at start.
    //
    // The "pick not reverse" rule resolves the collision when two boundary
    // edges start at the same merged vertex: one is the continuation of the
    // current face, the other is from the adjacent face. The reverse would
    // go BACK the way we came, so the correct one is the OTHER one.

    auto isBoundaryHE = [&](int heId) -> bool {
        const MGHalfEdge* he = halfEdge(heId);
        if (!he) return false;
        if (he->twin == -1) return true;
        const MGHalfEdge* tw = halfEdge(he->twin);
        return tw && tw->face == -1;
    };

    // Step 1: collect boundary edge ids
    std::vector<int> boundaryIds;
    for (const auto& he : halfEdges)
        if (isBoundaryHE(he.id)) boundaryIds.push_back(he.id);

    if (boundaryIds.empty()) return {};

    // End-vertex of a half-edge = start-vertex of its twin
    auto endVertOf = [&](int heId) -> int {
        const MGHalfEdge* he = halfEdge(heId);
        if (!he) return -1;
        if (he->twin == -1) return -1;
        const MGHalfEdge* tw = halfEdge(he->twin);
        return tw ? tw->vertex : -1;
    };

    // Step 2: build startVert -> [boundary he ids leaving that vert]
    std::unordered_map<int, std::vector<int>> vertToBoundaryHEs;
    for (int id : boundaryIds) {
        const MGHalfEdge* he = halfEdge(id);
        if (he) vertToBoundaryHEs[he->vertex].push_back(id);
    }

    // Step 3: walk
    BoundaryString bs;
    int startId = boundaryIds[0];
    int curId   = startId;
    int safety  = 0;
    int maxLen  = (int)boundaryIds.size() * 2 + 4;
    std::set<int> visited;

    do {
        if (visited.count(curId)) break;
        visited.insert(curId);

        const MGHalfEdge* he = halfEdge(curId);
        if (!he || ++safety > maxLen) break;

        // Record this edge
        BoundaryElement edgeElem;
        edgeElem.is_turn    = false;
        edgeElem.edge_id    = curId;
        edgeElem.edge_label = he->label.r;
        bs.elements.push_back(edgeElem);

        // Find end vertex
        int endVert = endVertOf(curId);
        if (endVert == -1) break;

        // Find next boundary edge at endVert.
        // Rules: must not be he->twin (that goes backward), must not be visited.
        auto it = vertToBoundaryHEs.find(endVert);
        if (it == vertToBoundaryHEs.end()) break;

        int nextId = -1;
        for (int cand : it->second) {
            if (cand == he->twin)    continue; // don't go backward
            if (visited.count(cand) && cand != startId) continue; // don't revisit
            nextId = cand;
            break;
        }

        if (nextId == -1) break;

        // Insert turn
        const MGHalfEdge* nextHe = halfEdge(nextId);
        if (nextHe) {
            float t0 = he->label.theta;
            float t1 = nextHe->label.theta;
            float cross = std::cos(t0)*std::sin(t1) - std::sin(t0)*std::cos(t1);
            if (std::abs(cross) > 1e-5f) {
                BoundaryElement turnElem;
                turnElem.is_turn   = true;
                turnElem.turn_type = (cross > 0.f) ? TurnType::Positive : TurnType::Negative;
                bs.elements.push_back(turnElem);
            }
        }

        if (nextId == startId) break; // loop closed

        curId = nextId;
    } while (curId != startId);

    return bs;
}

// ============================================================
// MerrellGraph — debug
// ============================================================

void MerrellGraph::dump() const
{
    std::cout << "[MerrellGraph] "
              << vertexCount() << "v  "
              << edgeCount()   << "e  "
              << faceCount()   << "f\n";

    for (const auto& v : vertices)
        std::cout << "  V" << v.id << "  (" << v.pos.x << "," << v.pos.y
                  << ")  outHE=" << v.outgoing_he << "\n";

    for (const auto& f : faces) {
        BoundaryString bs = boundaryOf(f.id);
        std::cout << "  F" << f.id << "  \"" << f.label << "\""
                  << "  deg=" << f.degree
                  << "  bnd=" << bs.toString()
                  << "  turns=" << bs.totalTurnCount()
                  << "  complete=" << (bs.isComplete() ? "Y" : "N") << "\n";
    }

    for (size_t i = 0; i + 1 < halfEdges.size(); i += 2) {
        const MGHalfEdge& he   = halfEdges[i];
        const MGHalfEdge& twin = halfEdges[i + 1];
        std::cout << "  HE" << he.id
                  << "  V" << he.vertex << "->V" << twin.vertex
                  << "  l=\"" << he.label.l << "\""
                  << "  r=\"" << he.label.r << "\""
                  << "  th=" << he.label.theta
                  << "  face=" << he.face
                  << "  twin=" << he.twin << "\n";
    }
}

} // namespace merrell

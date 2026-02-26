#include "MerrellGraph.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <unordered_map>

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
        if (e.is_turn)
            ss << (e.turn_type == TurnType::Positive ? "^" : "v");
        else
            ss << "E" << e.edge_id;
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
        // Find the half-edge whose next or prev points to id
        for (auto& other : halfEdges) {
            if (other.next == id) other.next = -1;
            if (other.prev == id) other.prev = -1;
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
        edgeElem.is_turn = false;
        edgeElem.edge_id = loop[i]->id;
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
    // Collect all half-edges that have no twin (twin == -1) or whose twin
    // has face == -1 (exterior). These form the outer boundary of the graph.
    // Walk the cut-edge chain: at each vertex, follow outgoing half-edges
    // to find the next cut edge on the outer boundary (CCW order).

    // Step 1: collect all "boundary" half-edge ids (r == "open" or twin == -1)
    std::vector<int> cutIds;
    for (const auto& he : halfEdges) {
        if (he.twin == -1 || he.label.r == "open")
            cutIds.push_back(he.id);
    }

    if (cutIds.empty()) return {};

    // Step 2: build a map from start-vertex to cut half-edge
    // so we can chain them in order
    std::unordered_map<int,int> vertToHe; // vertex id -> he id leaving that vertex on boundary
    for (int id : cutIds) {
        const MGHalfEdge* he = halfEdge(id);
        if (he) vertToHe[he->vertex] = id;
    }

    // Step 3: walk the chain starting from cutIds[0]
    BoundaryString bs;
    int startId = cutIds[0];
    int curId   = startId;
    int safety  = 0;

    do {
        const MGHalfEdge* he = halfEdge(curId);
        if (!he || ++safety > (int)cutIds.size() + 2) break;

        // Add edge element
        BoundaryElement edgeElem;
        edgeElem.is_turn = false;
        edgeElem.edge_id = curId;
        bs.elements.push_back(edgeElem);

        // Find the end vertex of this he (= start of its twin)
        const MGHalfEdge* tw = halfEdge(he->twin);
        int endVert = tw ? tw->vertex : -1;

        // Look up the next cut edge starting at endVert
        auto it = vertToHe.find(endVert);
        if (it == vertToHe.end()) break;

        int nextId = it->second;
        if (nextId == curId) break; // degenerate

        // Compute turn between this he and the next
        const MGHalfEdge* nextHe = halfEdge(nextId);
        if (nextHe) {
            float t0 = he->label.theta;
            float t1 = nextHe->label.theta;
            float cross = std::cos(t0) * std::sin(t1) - std::sin(t0) * std::cos(t1);
            if (std::abs(cross) > 1e-5f) {
                BoundaryElement turnElem;
                turnElem.is_turn   = true;
                turnElem.turn_type = (cross > 0.f) ? TurnType::Positive : TurnType::Negative;
                bs.elements.push_back(turnElem);
            }
        }

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

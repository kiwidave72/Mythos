#include "MerrellGrammar.h"
#include <iostream>
#include <set>
#include <unordered_map>
#include <algorithm>

namespace merrell {

// ============================================================
// MG-1: loadFromTiles
// ============================================================
//
// Disassembly (Sec 4.1). Builds one canonical MerrellGraph per unique tile
// label: unit-square face, 4 vertices, 4 half-edge pairs.
// Socket edges: label.r = "open".  Sealed edges: label.r = "exterior".

void MerrellGrammar::loadFromTiles(const std::vector<TileSocketDef>& socketDefs,
                                   const std::vector<TileInput>&     /*tiles*/)
{
    m_primitives.clear();
    m_lastError.clear();

    if (socketDefs.empty()) {
        m_lastError = "loadFromTiles: no socket definitions provided.";
        return;
    }

    for (const auto& def : socketDefs) {
        MerrellGraph prim;
        int faceId = prim.addFace(def.label);

        // Unit-square corners (Y up):
        // V0=top-left  V1=top-right  V2=bottom-right  V3=bottom-left
        int v0 = prim.addVertex({0.f, 1.f});
        int v1 = prim.addVertex({1.f, 1.f});
        int v2 = prim.addVertex({1.f, 0.f});
        int v3 = prim.addVertex({0.f, 0.f});

        std::set<std::pair<int,int>> socketSet;
        for (const auto& s : def.sockets)
            socketSet.insert({s.x, s.y});
        auto isSocket = [&](glm::ivec2 d) {
            return socketSet.count({d.x, d.y}) > 0;
        };

        // CCW face order: top → right → bottom → left
        struct EdgeInfo { int from, to; glm::ivec2 faceDir; glm::ivec2 travelDir; };
        EdgeInfo edges[4] = {
            { v0, v1, {0,-1}, {1, 0} },   // Top    faces N  travels E
            { v1, v2, {1, 0}, {0, 1} },   // Right  faces E  travels S
            { v2, v3, {0, 1}, {-1,0} },   // Bottom faces S  travels W
            { v3, v0, {-1,0}, {0,-1} },   // Left   faces W  travels N
        };

        std::vector<int> loop;
        loop.reserve(4);
        for (const auto& e : edges) {
            EdgeLabel lbl;
            lbl.l     = def.label;
            lbl.r     = isSocket(e.faceDir) ? "open" : "exterior";
            lbl.theta = gridDirToTheta(e.travelDir);
            loop.push_back(prim.addHalfEdgePair(e.from, e.to, lbl));
        }
        prim.linkFaceLoop(faceId, loop);
        m_primitives.push_back(std::move(prim));
    }

    std::cout << "[MerrellGrammar] MG-1 complete: "
              << m_primitives.size() << " primitives.\n";
}

void MerrellGrammar::loadFromShape(const MeshData* /*mesh*/)
{
    std::cout << "[MerrellGrammar] loadFromShape: unimplemented (MG-5)\n";
}

// ============================================================
// MG-3: Grammar extraction
// ============================================================

void MerrellGrammar::extractGrammar(std::function<void(int,int)> progressCb)
{
    m_rules.clear();
    m_lastError.clear();

    if (m_primitives.empty()) {
        m_lastError = "No primitives. Call loadFromTiles() first.";
        std::cerr << "[MerrellGrammar] " << m_lastError << "\n";
        return;
    }

    buildHierarchy(progressCb);
    algorithm1_findGrammar(progressCb);

    std::cout << "[MerrellGrammar] extractGrammar: "
              << m_rules.size() << " rules (TODO MG-3)\n";
}

// ============================================================
// MG-4: Shape generation
// ============================================================

void MerrellGrammar::generate(int seed,
                              std::function<void(int,int)> progressCb)
{
    m_result = {};
    m_lastError.clear();
    if (m_rules.empty()) {
        m_lastError = "No rules. Call extractGrammar() first.";
        return;
    }
    beginGenerate(seed);
    for (int i = 0; i < m_settings.maxIterations; ++i) {
        if (progressCb) progressCb(i, m_settings.maxIterations);
        if (stepGenerate()) break;
    }
}

void MerrellGrammar::beginGenerate(int seed)
{
    m_genSeed  = seed;
    m_genStep  = 0;
    m_genDone  = false;
    m_genState.clear();
    m_result   = {};
}

bool MerrellGrammar::stepGenerate()
{
    // TODO MG-4: Algorithm 3 step
    if (m_genDone) return true;
    if (++m_genStep >= m_settings.maxIterations) {
        m_genDone        = true;
        m_result.success = false;
        m_result.errorMsg = "Max iterations reached.";
        return true;
    }
    return false;
}

bool MerrellGrammar::saveRules(const std::string&) const
{
    std::cout << "[MerrellGrammar] saveRules: unimplemented (MG-5)\n";
    return false;
}

bool MerrellGrammar::loadRules(const std::string&)
{
    std::cout << "[MerrellGrammar] loadRules: unimplemented (MG-5)\n";
    return false;
}

// ============================================================
// MG-2: Hierarchy construction
// ============================================================
//
// Implements Sec 4.2 (gluing operations) and Sec 4.3 (hierarchy).
//
// Loop gluing (aā → ε, Sec 4.2 type 1):
//   Given two graphs A and B, each with a "cut" half-edge pair labelled (l,r):
//   if one has r="open" with label L, and the other also r="open" with the
//   same complementary label, the two can be glued together.
//   In practice for our grid-first primitives: any two tiles that have a
//   matching socket direction can be glued edge-to-edge.
//
//   Concretely:
//     1. Copy both graphs into a new combined graph (re-index ids).
//     2. Identify the glue vertices (weld the endpoint vertices together).
//     3. Remove the two glued half-edge pairs (they become interior).
//     4. Update boundary strings.
//
// Branch gluing (TODO MG-2 continuation):
//   More complex — deferred to Phase MG-2 step 2.
//
// For now: loop gluings only, generating gen-1 graphs from pairs of gen-0
// primitives, and gen-2 from pairs of gen-1 graphs, etc.

// Helper: copy graph `src` into `dst`, offsetting all ids by `vertOff`,
// `heOff`, `faceOff`. Returns the new face count added.
static void appendGraph(MerrellGraph& dst, const MerrellGraph& src,
                        int vertOff, int heOff, int faceOff)
{
    for (const auto& v : src.vertices) {
        MGVertex nv = v;
        nv.id          = v.id + vertOff;
        nv.outgoing_he = (v.outgoing_he >= 0) ? v.outgoing_he + heOff : -1;
        dst.vertices.push_back(nv);
    }

    for (const auto& he : src.halfEdges) {
        MGHalfEdge nhe = he;
        nhe.id     = he.id   + heOff;
        nhe.twin   = (he.twin >= 0)  ? he.twin  + heOff : -1;
        nhe.next   = (he.next >= 0)  ? he.next  + heOff : -1;
        nhe.prev   = (he.prev >= 0)  ? he.prev  + heOff : -1;
        nhe.vertex = he.vertex + vertOff;
        nhe.face   = (he.face >= 0)  ? he.face  + faceOff : -1;
        dst.halfEdges.push_back(nhe);
    }

    for (const auto& f : src.faces) {
        MGFace nf = f;
        nf.id       = f.id + faceOff;
        nf.start_he = (f.start_he >= 0) ? f.start_he + heOff : -1;
        dst.faces.push_back(nf);
    }
}

// Loop-glue two graphs A and B along matching open edges.
// heA_id: id of an "open" half-edge in A (after appendGraph offset applied).
// heB_id: id of an "open" half-edge in B (after appendGraph offset applied).
// Returns true on success, fills `result`.
static bool loopGlue(const MerrellGraph& A, const MerrellGraph& B,
                     int heA_localId, int heB_localId,
                     MerrellGraph& result)
{
    // Offsets for B's ids when appended into result (A is appended first)
    int vertOff = (int)A.vertices.size();
    int heOff   = (int)A.halfEdges.size();
    int faceOff = (int)A.faces.size();

    // Append A unchanged (zero offset)
    appendGraph(result, A, 0, 0, 0);
    // Append B with offset
    appendGraph(result, B, vertOff, heOff, faceOff);

    // Ids in the combined graph
    int heA = heA_localId;                   // already in A's index space (no offset)
    int heB = heB_localId + heOff;           // B's edge after offset

    MGHalfEdge* a  = result.halfEdge(heA);
    MGHalfEdge* b  = result.halfEdge(heB);
    if (!a || !b) return false;

    // The twin of heA in A (a_twin) and twin of heB in B (b_twin)
    int heA_twin = a->twin;
    int heB_twin = b->twin;

    MGHalfEdge* a_twin = result.halfEdge(heA_twin);
    MGHalfEdge* b_twin = result.halfEdge(heB_twin);
    if (!a_twin || !b_twin) return false;

    // Vertex ids in the combined graph:
    //   a goes v0_a → v1_a  (a->vertex is v0_a, a_twin->vertex is v1_a)
    //   b goes v0_b → v1_b
    //
    // After gluing: v1_a merges with v0_b (the "start" of b joins "end" of a)
    //               v0_a merges with v1_b (the "end" of b joins "start" of a)
    int v0_a = a->vertex;
    int v1_a = a_twin->vertex;
    int v0_b = b->vertex;
    int v1_b = b_twin->vertex;

    // The edges point in opposite directions along the shared boundary, so:
    //   end of a = start of b  →  v1_a ← v0_b
    //   start of a = end of b  →  v0_a ← v1_b
    result.mergeVertices(v0_b, v1_a);
    result.mergeVertices(v1_b, v0_a);

    // Make heA and heB proper twins (connecting across the gluing seam)
    // so the interior edge is now a proper internal edge with both faces.
    // Then mark them as no longer "open" — they are now interior.
    a->twin       = heB;
    b->twin       = heA;
    a_twin->twin  = heB_twin;
    b_twin->twin  = heA_twin;

    // Update labels: the glued edges are no longer "open"
    a->label.r       = "glued";
    b->label.r       = "glued";
    a_twin->label.r  = "glued";
    b_twin->label.r  = "glued";

    return true;
}

void MerrellGrammar::buildHierarchy(std::function<void(int,int)> progressCb)
{
    m_hierarchy.clear();

    // ---- Gen 0: seed with primitives ----------------------------------------
    for (const auto& prim : m_primitives) {
        HierarchyNode node;
        node.id         = nextHierarchyId();
        node.generation = 0;
        node.graph      = prim;
        if (!prim.faces.empty())
            node.boundary = prim.boundaryOf(prim.faces[0].id);
        node.isComplete = node.boundary.isComplete();
        m_hierarchy.push_back(std::move(node));
    }

    int totalSteps = m_settings.maxHierarchyGen * (int)m_primitives.size();
    int step = 0;

    // ---- Gen 1+: loop gluings -----------------------------------------------
    for (int gen = 0; gen < m_settings.maxHierarchyGen; ++gen) {
        if (progressCb) progressCb(step++, totalSteps);
        tryLoopGluings(gen);
        tryBranchGluings(gen);

        // Stop if no new nodes were added at this generation
        bool anyNew = false;
        for (const auto& n : m_hierarchy)
            if (n.generation == gen + 1) { anyNew = true; break; }
        if (!anyNew) break;
    }

    // Cache boundary strings for all new nodes
    for (auto& node : m_hierarchy) {
        if (node.boundary.isEmpty() && !node.graph.isEmpty()) {
            if (!node.graph.faces.empty())
                node.boundary = node.graph.outerBoundary();
            if (node.boundary.isEmpty() && !node.graph.faces.empty())
                node.boundary = node.graph.boundaryOf(node.graph.faces[0].id);
            node.isComplete = node.boundary.isComplete();
        }
    }

    std::cout << "[MerrellGrammar] buildHierarchy: "
              << m_hierarchy.size() << " nodes total, depth="
              << hierarchyDepth() << "\n";

    (void)progressCb;
}

void MerrellGrammar::tryLoopGluings(int generation)
{
    // Collect all hierarchy nodes at `generation`
    std::vector<int> genNodes;
    for (int i = 0; i < (int)m_hierarchy.size(); ++i)
        if (m_hierarchy[i].generation == generation)
            genNodes.push_back(i);

    if (genNodes.empty()) return;

    // Track boundary strings of already-produced glued results so we don't
    // produce duplicates (Sec 5.7: bound hierarchy by unique boundary strings).
    // We use toString() as a cheap proxy for circular equality.
    // (Full circular equality check deferred to Algorithm 2.)
    std::set<std::string> seenBoundaries;
    for (const auto& n : m_hierarchy)
        if (!n.boundary.isEmpty())
            seenBoundaries.insert(n.boundary.toString());

    int maxNewNodes = m_settings.maxRules; // use as a safety cap
    int newNodes    = 0;

    // Try all pairs (including self-gluing) at this generation
    for (int ai : genNodes) {
        for (int bi : genNodes) {
            if (newNodes >= maxNewNodes) goto done;

            const MerrellGraph& A = m_hierarchy[ai].graph;
            const MerrellGraph& B = m_hierarchy[bi].graph;

            // Find all "open" half-edges in A
            std::vector<int> openA, openB;
            for (const auto& he : A.halfEdges)
                if (he.label.r == "open") openA.push_back(he.id);
            for (const auto& he : B.halfEdges)
                if (he.label.r == "open") openB.push_back(he.id);

            if (openA.empty() || openB.empty()) continue;

            // Try each pairing of one open edge from A with one from B
            for (int heA : openA) {
                for (int heB : openB) {
                    const MGHalfEdge* edgeA = A.halfEdge(heA);
                    const MGHalfEdge* edgeB = B.halfEdge(heB);
                    if (!edgeA || !edgeB) continue;

                    // Compatibility: the two open edges must have complementary
                    // labels. For our grid-first tiles, both have l=tile_label
                    // and r="open". They are compatible if their thetas differ
                    // by exactly pi (they face opposite directions).
                    float thetaDiff = std::abs(edgeA->label.theta - edgeB->label.theta);
                    bool complementary =
                        std::abs(thetaDiff - MG_PI) < 1e-4f ||
                        std::abs(thetaDiff - MG_PI - 2.f * MG_PI) < 1e-4f;

                    if (!complementary) continue;

                    // Attempt the gluing
                    MerrellGraph result;
                    if (!loopGlue(A, B, heA, heB, result)) continue;

                    // Compute the boundary of the result
                    BoundaryString bs = result.outerBoundary();
                    // Fallback: if multi-face, try to get the combined boundary
                    if (bs.isEmpty() && !result.faces.empty())
                        bs = result.boundaryOf(result.faces[0].id);

                    // Dedup by boundary string representation
                    std::string bsStr = bs.toString();
                    if (seenBoundaries.count(bsStr)) continue;
                    seenBoundaries.insert(bsStr);

                    // Add to hierarchy
                    HierarchyNode node;
                    node.id         = nextHierarchyId();
                    node.generation = generation + 1;
                    node.graph      = std::move(result);
                    node.boundary   = bs;
                    node.isComplete = bs.isComplete();
                    node.childIds   = { ai, bi };

                    std::cout << "[MerrellGrammar] MG-2 loop-glue: gen "
                              << generation + 1 << " node " << node.id
                              << "  boundary=" << bs.toString()
                              << "  complete=" << (node.isComplete ? "Y" : "N")
                              << "\n";

                    m_hierarchy.push_back(std::move(node));
                    ++newNodes;
                }
            }
        }
    }
    done:;
}

void MerrellGrammar::tryBranchGluings(int generation)
{
    // TODO MG-2 step 2: branch gluing — (āB to a) replaces a → Bv  (Sec 4.2)
    // Branch gluing inserts a subgraph B alongside a cut edge a.
    // More complex than loop gluing; deferred until loop gluings are verified.
    (void)generation;
}

// ============================================================
// MG-3: Algorithms 1 & 2
// ============================================================

void MerrellGrammar::algorithm1_findGrammar(
    std::function<void(int,int)> progressCb)
{
    // TODO MG-3: Algorithm 1 (Sec 5.1)
    (void)progressCb;
}

bool MerrellGrammar::algorithm2_findMatchingGroups(int hierarchyNodeId)
{
    // TODO MG-3: Algorithm 2 (Sec 5.3)
    (void)hierarchyNodeId;
    return false;
}

// ============================================================
// MG-4: Position solving & matching
// ============================================================

bool MerrellGrammar::solvePositions(MerrellGraph& graph)
{
    // TODO MG-4: Sec 6.2 linear system
    (void)graph;
    return false;
}

RuleMatch MerrellGrammar::findMatch(const DPORule& rule,
                                    const MerrellGraph& G,
                                    int seed) const
{
    // TODO MG-4
    (void)rule; (void)G; (void)seed;
    return {};
}

bool MerrellGrammar::applyRule(const DPORule& rule,
                               const RuleMatch& match,
                               MerrellGraph& G)
{
    // TODO MG-4
    (void)rule; (void)match; (void)G;
    return false;
}

// ============================================================
// Queries
// ============================================================

int MerrellGrammar::hierarchyDepth() const
{
    int d = 0;
    for (const auto& n : m_hierarchy)
        if (n.generation > d) d = n.generation;
    return d;
}

} // namespace merrell

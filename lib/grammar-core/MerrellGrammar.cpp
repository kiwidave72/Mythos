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
              << m_rules.size() << " rules extracted (MG-3 complete).\n";
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

    // ---- Translate B's vertex positions so it connects spatially to A ----
    // heA_localId travels in direction theta_A. B's heB_localId must travel
    // in the opposite direction (theta_B ≈ theta_A + π).
    // We want B's "start" vertex (heB->vertex) to land at A's "end" vertex.
    // A's end vertex = A's twin of heA start vertex.
    {
        const MGHalfEdge* heA_orig = A.halfEdge(heA_localId);
        const MGHalfEdge* heB_orig = B.halfEdge(heB_localId);
        if (heA_orig && heB_orig) {
            // A's end-vertex position
            const MGHalfEdge* heA_tw = A.halfEdge(heA_orig->twin);
            if (heA_tw) {
                const MGVertex* vA_end = A.vertex(heA_tw->vertex);
                const MGVertex* vB_start = B.vertex(heB_orig->vertex);
                if (vA_end && vB_start) {
                    glm::vec2 offset = vA_end->pos - vB_start->pos;
                    // Apply to all of B's vertices before appending
                    // We do this via a modified appendGraph that adds the offset
                    for (const auto& v : B.vertices) {
                        MGVertex nv = v;
                        nv.id          = v.id + vertOff;
                        nv.outgoing_he = (v.outgoing_he >= 0) ? v.outgoing_he + heOff : -1;
                        nv.pos         = v.pos + offset;
                        result.vertices.push_back(nv);
                    }
                    for (const auto& he : B.halfEdges) {
                        MGHalfEdge nhe = he;
                        nhe.id     = he.id   + heOff;
                        nhe.twin   = (he.twin >= 0)  ? he.twin  + heOff : -1;
                        nhe.next   = (he.next >= 0)  ? he.next  + heOff : -1;
                        nhe.prev   = (he.prev >= 0)  ? he.prev  + heOff : -1;
                        nhe.vertex = he.vertex + vertOff;
                        nhe.face   = (he.face >= 0)  ? he.face  + faceOff : -1;
                        result.halfEdges.push_back(nhe);
                    }
                    for (const auto& f : B.faces) {
                        MGFace nf = f;
                        nf.id       = f.id + faceOff;
                        nf.start_he = (f.start_he >= 0) ? f.start_he + heOff : -1;
                        result.faces.push_back(nf);
                    }
                    goto vertices_appended;
                }
            }
        }
    }
    // Fallback: append B without position offset
    appendGraph(result, B, vertOff, heOff, faceOff);
    vertices_appended:;

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

    // Re-fetch pointers after vertex merge (vectors may not have reallocated but
    // erasing elements can shift indices — use id lookup)
    a       = result.halfEdge(heA);
    b       = result.halfEdge(heB);
    a_twin  = result.halfEdge(heA_twin);
    b_twin  = result.halfEdge(heB_twin);
    if (!a || !b || !a_twin || !b_twin) return false;

    // ---- Stitch face loops across the seam ----
    // Before gluing:
    //   ... → a_prev → [a] → a_next → ...   (face of A)
    //   ... → b_prev → [b] → b_next → ...   (face of B)
    //   (a and b's twins carry the face-loop info for their respective faces)
    //
    // After gluing, the two faces share the seam edge. We need to connect:
    //   a_twin->prev's next  →  b->next   (A's inner face continues into B)
    //   b_twin->prev's next  →  a->next   (B's inner face continues into A)
    // i.e. remove a and b from their loops and cross-link.
    {
        int a_prev_id = a->prev;
        int a_next_id = a->next;
        int b_prev_id = b->prev;
        int b_next_id = b->next;

        MGHalfEdge* a_prev = (a_prev_id >= 0) ? result.halfEdge(a_prev_id) : nullptr;
        MGHalfEdge* a_next = (a_next_id >= 0) ? result.halfEdge(a_next_id) : nullptr;
        MGHalfEdge* b_prev = (b_prev_id >= 0) ? result.halfEdge(b_prev_id) : nullptr;
        MGHalfEdge* b_next = (b_next_id >= 0) ? result.halfEdge(b_next_id) : nullptr;

        // Cross-link: A's face flows into B's face through the seam
        if (a_prev && b_next) { a_prev->next = b_next_id; b_next->prev = a_prev_id; }
        if (b_prev && a_next) { b_prev->next = a_next_id; a_next->prev = b_prev_id; }

        // Fix face start_he pointers so they don't start on the glued edge
        for (auto& f : result.faces) {
            if (f.start_he == heA) f.start_he = (a_next_id >= 0) ? a_next_id : -1;
            if (f.start_he == heB) f.start_he = (b_next_id >= 0) ? b_next_id : -1;
        }
    }

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

    // CRITICAL: Assign face IDs to the "exterior twin" half-edges of the seam
    // (a_twin and b_twin had face=-1 since they were the outer side of the
    // open edges before gluing). After gluing they are interior seam edges.
    // outerBoundary() uses: isBoundaryHE(id) = (twin->face == -1).
    // Without this fix, a_twin's new twin is b_twin (face=-1), so a_twin
    // is wrongly identified as a boundary edge, corrupting the boundary walk.
    //
    // Cross-assign: a_twin is now "inside" face_B, b_twin is inside face_A.
    if (a_twin->face == -1) a_twin->face = b->face;   // a_twin interior to B's face
    if (b_twin->face == -1) b_twin->face = a->face;   // b_twin interior to A's face

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

    // ---- Diagnostic dump — full hierarchy --------------------------------
    std::cout << "\n=== PRIMITIVE DUMP ===\n";
    for (int i = 0; i < (int)m_primitives.size(); ++i) {
        const auto& p = m_primitives[i];
        std::cout << "PRIM[" << i << "]";
        if (!p.faces.empty()) std::cout << " label=" << p.faces[0].label;
        std::cout << "  v=" << p.vertexCount()
                  << " e=" << p.edgeCount()
                  << " f=" << p.faceCount() << "\n";
        for (const auto& he : p.halfEdges) {
            if (he.face == -1) continue; // skip twins with no face
            std::cout << "  HE" << he.id
                      << "  r=" << he.label.r
                      << "  theta=" << (he.label.theta * 180.f / 3.14159f) << "deg"
                      << "  twin=" << he.twin << "\n";
        }
        if (!p.faces.empty()) {
            BoundaryString bs = p.boundaryOf(p.faces[0].id);
            std::cout << "  boundary=" << bs.toString()
                      << "  turns=" << bs.totalTurnCount()
                      << "  complete=" << (bs.isComplete() ? "YES" : "NO") << "\n";
        }
    }

    std::cout << "\n=== HIERARCHY DUMP ===\n";
    for (const auto& node : m_hierarchy) {
        // Count edge types
        int nOpen = 0, nExt = 0, nGlued = 0;
        for (const auto& he : node.graph.halfEdges) {
            if (he.label.r == "open")     ++nOpen;
            if (he.label.r == "exterior") ++nExt;
            if (he.label.r == "glued")    ++nGlued;
        }

        // Outer boundary (structural walk — the full perimeter)
        merrell::BoundaryString outerBs = node.graph.outerBoundary();

        std::cout << "NODE " << node.id
                  << "  gen=" << node.generation
                  << "  v=" << node.graph.vertexCount()
                  << " e=" << node.graph.edgeCount()
                  << " f=" << node.graph.faceCount()
                  << "  complete=" << (node.isComplete ? "YES" : "NO ")
                  << "  pruned="  << (node.pruned     ? "YES" : "NO ")
                  << "\n";
        std::cout << "  outer_bnd=" << outerBs.toString()
                  << "  turns=" << outerBs.totalTurnCount()
                  << "  outer_complete=" << (outerBs.isComplete() ? "YES" : "NO");
        if (!node.parentIds.empty()) {
            std::cout << "  parents=[";
            for (int id : node.parentIds) std::cout << id << " ";
            std::cout << "]";
        }
        std::cout << "\n";
        std::cout << "  edges: open=" << nOpen
                  << " exterior=" << nExt
                  << " glued=" << nGlued << "\n";
    }
    std::cout << "=== END DUMP ===\n\n";

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
                    node.parentIds   = { ai, bi };

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
//
// Algorithm 1 (Sec 5.1) — Grammar extraction
// ─────────────────────────────────────────────────────────────
// Extracts a minimal set of DPO rewrite rules from the hierarchy.
// Each rule has the form  L ← I → R  where:
//   R = a smaller/simpler graph (matched during generation)
//   L = a larger graph (grown to during generation)
//   I = the interface (shared boundary, preserved by the rule)
//
// For our grid-first, loop-glue-only hierarchy:
//
//  STARTER rules (Sec 5.3.1):  ∅ → primitive
//    One per gen-0 primitive. R = empty graph, L = primitive.
//    I = empty. Initialises generation.
//
//  EXPANSION rules:  parent → child
//    For each gen-1+ hierarchy node C formed by gluing (A, B):
//      R = A (one parent graph, with its open edge)
//      L = C (the glued result)
//      I = the interface = the open edge pair that was consumed
//    In generation (R→L direction):
//      "Find an open edge in the current shape matching A's open edge,
//       then attach B to grow to C."
//
// Deduplication (Sec 5.7):
//   Rules are deduped by the pair (boundary(R), boundary(L)).
//   Two rules with the same boundary pair are equivalent.
//
// Pruning (Sec 5.6):
//   A hierarchy node is pruned if it has no complete descendant.
//   Pruned nodes never produce generation termination — skip them.

// ── helpers ──────────────────────────────────────────────────

// Build a minimal 2-vertex, 1-edge-pair interface graph I.
// The interface for a single loop-glue is one edge connecting two vertices.
// Used as the shared boundary preserved by an expansion rule.
static MerrellGraph buildInterfaceGraph(float v0x, float v0y,
                                        float v1x, float v1y,
                                        const EdgeLabel& label)
{
    MerrellGraph I;
    int iv0 = I.addVertex({v0x, v0y});
    int iv1 = I.addVertex({v1x, v1y});
    I.addHalfEdgePair(iv0, iv1, label);
    return I;
}

// Build a Starter rule:  ∅ → primitive
//   L = the primitive graph (a single tile)
//   R = empty graph
//   I = empty graph
//   phi_L and phi_R are empty morphisms
static DPORule buildStarterRule(int ruleId, const HierarchyNode& node)
{
    DPORule rule;
    rule.id   = ruleId;
    rule.kind = RuleKind::Starter;
    rule.name = "starter_" + (node.graph.faces.empty()
                               ? std::string("prim")
                               : node.graph.faces[0].label);
    rule.isStarterRule           = true;
    rule.extractedAtGeneration   = 0;

    rule.L = node.graph;      // the full primitive
    rule.R = MerrellGraph();  // empty
    rule.I = MerrellGraph();  // empty interface

    // Boundary of L = the primitive's outer boundary
    rule.boundary_L = node.boundary;
    // Boundary of R = empty
    rule.boundary_R = BoundaryString{};

    return rule;
}

// Build an Expansion rule:  parent → child
//   L = child graph (the glued result)
//   R = parent graph A (what we match, with its open edge)
//   I = single open-edge pair (the seam that was consumed)
//
// parentNodeA: the "A" parent (the one whose open edge was consumed).
// childNode:   the glued result.
// We record which open edge of A was consumed by looking at what
// became 'glued' in child that was 'open' in A.
//
// phi_L: I → L   maps I's edge to the glued seam in L
// phi_R: I → R   maps I's edge to the still-open edge in R (= A's open edge)
static DPORule buildExpansionRule(int ruleId,
                                  const HierarchyNode& parentNodeA,
                                  const HierarchyNode& childNode,
                                  int /*gluedHeInChild*/)
{
    DPORule rule;
    rule.id   = ruleId;
    rule.kind = RuleKind::LoopGlue;
    rule.name = "expand_" + std::to_string(parentNodeA.id)
              + "_to_"    + std::to_string(childNode.id);
    rule.extractedAtGeneration = childNode.generation;

    // L = child, R = parentA
    rule.L = childNode.graph;
    rule.R = parentNodeA.graph;

    rule.boundary_L = childNode.boundary;
    rule.boundary_R = parentNodeA.boundary;

    // Interface I: a simple open-edge graph. For a loop-glue the interface
    // is the edge pair that was glued. We build a minimal 2-vertex graph
    // representing one open edge (the shape of a "slot" that R exposes).
    //
    // Find an open edge in R (parentA) to use as I's template.
    const MGHalfEdge* openHE = nullptr;
    for (const auto& he : rule.R.halfEdges)
        if (he.label.r == "open" && he.face >= 0) { openHE = &he; break; }

    if (openHE) {
        const MGVertex* v0 = rule.R.vertex(openHE->vertex);
        const MGHalfEdge* twin = rule.R.halfEdge(openHE->twin);
        const MGVertex* v1 = twin ? rule.R.vertex(twin->vertex) : nullptr;

        if (v0 && v1) {
            rule.I = buildInterfaceGraph(v0->pos.x, v0->pos.y,
                                         v1->pos.x, v1->pos.y,
                                         openHE->label);

            // phi_R: I edge 0 → R's open edge
            rule.phi_R.vertexMap[0]   = v0->id;
            rule.phi_R.vertexMap[1]   = v1->id;
            rule.phi_R.halfEdgeMap[0] = openHE->id;
            rule.phi_R.halfEdgeMap[1] = openHE->twin;

            // phi_L: I edge 0 → the corresponding glued seam in L
            // Find the glued edge in L that has the same theta as the open edge in R.
            // (After gluing, the consumed open edge becomes 'glued' in L.)
            for (const auto& he : rule.L.halfEdges) {
                if (he.label.r == "glued" && he.face >= 0) {
                    float tDiff = std::abs(he.label.theta - openHE->label.theta);
                    bool same = tDiff < 1e-3f || std::abs(tDiff - 2.f*MG_PI) < 1e-3f;
                    if (same) {
                        const MGHalfEdge* ltwin = rule.L.halfEdge(he.twin);
                        const MGVertex* lv0 = rule.L.vertex(he.vertex);
                        const MGVertex* lv1 = ltwin ? rule.L.vertex(ltwin->vertex) : nullptr;
                        if (lv0 && lv1) {
                            rule.phi_L.vertexMap[0]   = lv0->id;
                            rule.phi_L.vertexMap[1]   = lv1->id;
                            rule.phi_L.halfEdgeMap[0] = he.id;
                            rule.phi_L.halfEdgeMap[1] = he.twin;
                        }
                        break;
                    }
                }
            }
        }
    }

    return rule;
}

// Returns true if `node` has any complete descendant (including itself).
// Used for pruning (Sec 5.6).
static bool hasCompleteDescendant(int nodeIdx,
                                  const std::vector<HierarchyNode>& hier)
{
    if (nodeIdx < 0 || nodeIdx >= (int)hier.size()) return false;
    if (hier[nodeIdx].isComplete) return true;
    // Search recursively through children (nodes whose parentIds include nodeIdx)
    for (int i = 0; i < (int)hier.size(); ++i) {
        const auto& n = hier[i];
        for (int pid : n.parentIds) {
            if (pid == nodeIdx && hasCompleteDescendant(i, hier))
                return true;
        }
    }
    return false;
}

// ── Algorithm 1 ─────────────────────────────────────────────

void MerrellGrammar::algorithm1_findGrammar(
    std::function<void(int,int)> progressCb)
{
    m_rules.clear();

    int totalNodes = (int)m_hierarchy.size();

    // ── Step 1: Starter rules for gen-0 primitives ───────────────────────────
    for (int i = 0; i < (int)m_hierarchy.size(); ++i) {
        const auto& node = m_hierarchy[i];
        if (node.generation != 0) continue;
        if (node.pruned)          continue;
        if (!node.isComplete)     continue;  // prims should always be complete

        DPORule r = buildStarterRule(nextRuleId(), node);
        std::cout << "[MG-3] Starter rule " << r.id
                  << ": ∅ → " << r.name << "\n";
        m_rules.push_back(std::move(r));

        if (progressCb) progressCb(i, totalNodes);
    }

    // ── Step 2: Expansion rules from gen-1+ nodes ────────────────────────────
    // Track seen (R_boundary, L_boundary) pairs to deduplicate.
    std::set<std::pair<std::string,std::string>> seenRulePairs;

    // Pre-seed with starter rule pairs (∅ → prim)
    for (const auto& r : m_rules)
        seenRulePairs.insert({r.boundary_R.toString(), r.boundary_L.toString()});

    for (int i = 0; i < (int)m_hierarchy.size(); ++i) {
        const auto& childNode = m_hierarchy[i];
        if (childNode.generation == 0) continue;
        if (childNode.pruned)          continue;
        if (!childNode.isComplete)     continue;  // only complete nodes produce rules

        if (progressCb) progressCb(i, totalNodes);

        // Each parent gives a potential expansion rule.
        // parentIds = [ai, bi] — gluing of A and B produced this child.
        // Rule: R=A, L=child, I=seam
        // We emit one rule per unique (∂A, ∂child) pair.
        for (int parentIdx : childNode.parentIds) {
            if (parentIdx < 0 || parentIdx >= (int)m_hierarchy.size()) continue;
            const auto& parentNode = m_hierarchy[parentIdx];
            if (parentNode.pruned) continue;

            std::string rBnd = parentNode.boundary.toString();
            std::string lBnd = childNode.boundary.toString();
            auto key = std::make_pair(rBnd, lBnd);
            if (seenRulePairs.count(key)) continue;
            seenRulePairs.insert(key);

            DPORule rule = buildExpansionRule(nextRuleId(), parentNode, childNode, -1);
            std::cout << "[MG-3] Expansion rule " << rule.id
                      << ": N" << parentIdx << " → N" << i
                      << "  (∂R=" << rBnd << "  ∂L=" << lBnd << ")\n";
            m_rules.push_back(std::move(rule));
        }
    }

    // ── Step 3: Pruning pass (Sec 5.6) ───────────────────────────────────────
    // Mark nodes with no complete descendants as pruned.
    int pruned = 0;
    for (int i = 0; i < (int)m_hierarchy.size(); ++i) {
        auto& node = m_hierarchy[i];
        if (!node.pruned && !hasCompleteDescendant(i, m_hierarchy)) {
            node.pruned = true;
            ++pruned;
        }
    }
    if (pruned > 0)
        std::cout << "[MG-3] Pruned " << pruned << " hierarchy nodes.\n";

    std::cout << "[MerrellGrammar] extractGrammar: "
              << m_rules.size() << " rules extracted.\n";

    if (progressCb) progressCb(totalNodes, totalNodes);
}

bool MerrellGrammar::algorithm2_findMatchingGroups(int hierarchyNodeId)
{
    // Algorithm 2 (Sec 5.3): recursive divide-and-conquer on boundary strings.
    // Used by Algorithm 1 to check if a graph can be reduced by existing rules.
    //
    // For our grid-first implementation, the simpler approach in algorithm1 above
    // (direct parent→child rule extraction) is equivalent for loop-glue-only
    // hierarchies. Algorithm 2 becomes essential for branch gluings (MG-2 step 2)
    // and when we need to detect if a large graph is reducible by a chain of
    // existing small rules rather than needing a new rule.
    //
    // TODO MG-3 step 2: implement full Algorithm 2 for completeness.
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

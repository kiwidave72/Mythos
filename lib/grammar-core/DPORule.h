#pragma once
// DPORule — Double Pushout (DPO) graph rewrite rule.
//
// Reference: Merrell 2023, Sec 2 (background), Sec 4.2 (branch/loop gluing),
// Sec 5 (grammar extraction), Sec 6 (generation).
//
// A DPO rule consists of three graphs and two morphisms:
//
//     φL          φR
//   L ←── I ──→ R
//
//   L = left graph  (the pattern to match and replace)
//   R = right graph (the replacement)
//   I = interface   (the shared part preserved by both sides)
//   φL = injective graph morphism  I → L
//   φR = injective graph morphism  I → R
//
// CONSTRUCTIVE application (R → L): grows the graph. Used in generation.
//   Match R in the current graph G → replace with L, gluing along I.
// DESTRUCTIVE application (L → R): shrinks the graph. Used in extraction.
//   Match L in G → replace with R.
//
// IMPORTANT: Morphisms are stored as index maps (element id in I → element
// id in L or R). This keeps the representation simple and avoids pointer
// invalidation when rules are copied or serialised.
//
// ⚠️  GRID-FIRST NOTE:
// DPORule knows nothing about grid cells or world positions.
// Position solving (Sec 6.2 linear system) is a separate concern layered
// on top in MerrellGrammar. Do not add position fields here.
//
// No OpenGL, no ImGui, no GLFW dependency.

#include "MerrellGraph.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace merrell {

// ============================================================
// GraphMorphism
// ============================================================
// An injective map from elements of one graph (source) to another (target).
// Stores separate maps for vertices, half-edges, and faces.
// All ids reference elements within their respective graph objects.
//
// A morphism is valid if:
//   - Every source vertex maps to a distinct target vertex.
//   - Every source half-edge maps to a distinct target half-edge.
//   - The mapping respects adjacency: if he is in face f in source,
//     then morphism(he) is in morphism(f) in target.
struct GraphMorphism {
    std::unordered_map<int,int> vertexMap;    // source vertex id   → target vertex id
    std::unordered_map<int,int> halfEdgeMap;  // source half-edge id → target half-edge id
    std::unordered_map<int,int> faceMap;      // source face id      → target face id

    // Returns true if all source elements are mapped (fully defined morphism).
    bool isTotal(const MerrellGraph& source) const;

    // Returns true if all mappings are injective (no two source elements
    // map to the same target element).
    bool isInjective() const;
};

// ============================================================
// RuleKind
// ============================================================
// What structural role this rule plays.
// Used by Algorithm 1 to guide rule extraction (Sec 5.2, 5.3).
enum class RuleKind {
    LoopGlue,       // Sec 4.2: two cut edges with same label, glue their endpoints.
                    //   aā → ε   (removes an interior edge, merging two faces)
    BranchGlue,     // Sec 4.2: branch graph B glued to edge a.
                    //   (āB to a) replaces a → B∨  (adds a sub-structure)
    Starter,        // Sec 5.3.1: base case — complete graph → empty ∅.
                    //   The "start rule" used to initialise generation.
    Stub,           // Sec 5.5: one-half-edge graph, used for pruning.
    General,        // All other extracted rules from Algorithm 1.
};

// ============================================================
// DPORule
// ============================================================
// A single Double Pushout rewrite rule.
//
// Storage layout: L, I, R are MerrellGraph objects owned by this rule.
// Morphisms phi_L and phi_R map from I into L and R respectively.
//
// Generation (Algorithm 3, Sec 6) uses constructive application (R→L):
//   1. Find a match of R in the current graph G (subgraph isomorphism).
//   2. Compute the pushout complement: remove R\φR(I) from G.
//   3. Glue in L\φL(I), connecting at the I boundary.
//
// Extraction (Algorithm 1, Sec 5) uses destructive application (L→R):
//   1. Find a match of L in G.
//   2. Replace with R, preserving I.
struct DPORule {
    int         id   = -1;
    std::string name;        // human-readable: "loop_wall_door", "branch_3", etc.
    RuleKind    kind = RuleKind::General;

    MerrellGraph  L;          // left graph  (larger, matched in G during extraction)
    MerrellGraph  R;          // right graph (smaller, matched in G during generation)
    MerrellGraph  I;          // interface graph (shared part)

    GraphMorphism phi_L;      // I → L  (which elements of I map to which in L)
    GraphMorphism phi_R;      // I → R  (which elements of I map to which in R)

    // ---- Boundary strings (cached for matching speed) ----------------------
    // Sec 5.2: matching is done via boundary strings.
    // Cached here after extraction to avoid recomputation during generation.
    BoundaryString boundary_L;   // ∂L — boundary of L
    BoundaryString boundary_R;   // ∂R — boundary of R

    // ---- Metadata ----------------------------------------------------------
    int  extractedAtGeneration = -1;  // hierarchy generation this rule came from
    bool isStarterRule         = false;  // true if R is the empty graph ∅

    // ---- Validity check ----------------------------------------------------
    // Returns true if phi_L and phi_R are valid injective morphisms and
    // L, I, R are internally consistent.
    bool isValid() const;

    // ---- Debug -------------------------------------------------------------
    void dump() const;  // print rule structure to stdout
};

// ============================================================
// RuleMatch
// ============================================================
// Result of matching a rule's R (or L) graph against a target graph G.
// Returned by MerrellGrammar::findMatch() during generation.
//
// The morphism maps R (or L) elements to G elements.
struct RuleMatch {
    int          ruleId   = -1;
    bool         valid    = false;
    GraphMorphism morphism;   // R (or L) → G
};

} // namespace merrell

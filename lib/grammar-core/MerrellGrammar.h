#pragma once
// MerrellGrammar — top-level Merrell 2023 algorithm controller.
//
// Reference: Paul Merrell, "Example-Based Procedural Modeling Using Graph
// Grammars", ACM Trans. Graph. 42, 4, Article 1 (August 2023).
//
// Algorithms:
//   Algorithm 1 (Sec 5)   — findGrammarFromPrimitives  (extracts DPO rules)
//   Algorithm 2 (Sec 5.3) — findMatchingGroups         (recursive rule finder)
//   Algorithm 3 (Sec 6)   — generate                   (produces new shapes)
//
// Usage sequence:
//   MerrellGrammar g;
//   g.loadFromTiles(socketDefs, tiles);  // MG-1
//   g.extractGrammar();                  // MG-3
//   g.generate(seed);                    // MG-4
//
// GRID-FIRST: loadFromTiles() is the grid-phase entry point.
//             loadFromShape() is declared but unimplemented until MG-5.
//
// No OpenGL, no ImGui, no GLFW dependency.

#include "MerrellGraph.h"
#include "DPORule.h"
#include <string>
#include <vector>
#include <functional>

// Forward declare — full definition not needed until MG-5
struct MeshData;

namespace merrell {

// ============================================================
// TileInput
// ============================================================
// Grid-phase descriptor for one placed tile instance.
// GRID-SPECIFIC — replaced by MeshData at MG-5.
struct TileInput {
    std::string label;    // e.g. "HStraight"
    glm::ivec2  cell;     // grid position (col, row)
    int         rotation; // 0/90/180/270 degrees clockwise
};

// ============================================================
// TileSocketDef
// ============================================================
// Socket layout for one tile type. Built from Grammar::PrimDef.
// Passed to loadFromTiles() alongside the tile instance list.
struct TileSocketDef {
    std::string             label;    // tile type name
    std::vector<glm::ivec2> sockets;  // socket directions e.g. {(1,0),(-1,0)}
};

// ============================================================
// PlacedFace / GenerationResult
// ============================================================
// Output of Algorithm 3. Editor reads this to populate the scene (MG-5).
// pos is vec2 for grid phase — promote to vec3 at MG-6.
struct PlacedFace {
    int         faceId   = -1;
    std::string label;
    glm::vec2   pos;            // [promote to vec3 at MG-6]
    float       rotation = 0.f; // radians [keep as float]
};

struct GenerationResult {
    MerrellGraph            graph;
    std::vector<PlacedFace> placed;
    bool                    success  = false;
    std::string             errorMsg;
};

// ============================================================
// GrammarSettings
// ============================================================
struct GrammarSettings {
    int   seed            = 42;
    int   maxIterations   = 4000;
    float minEdgeLength   = 0.5f;
    float maxEdgeLength   = 2.0f;
    int   maxHierarchyGen = 6;
    int   maxRules        = 200;
};

// ============================================================
// HierarchyNode
// ============================================================
// One node in the graph hierarchy (Sec 4.3), built in MG-2.
// gen 0 = primitives, gen N = graphs composed by N gluings.
struct HierarchyNode {
    int            id         = -1;
    int            generation = 0;
    MerrellGraph   graph;
    BoundaryString boundary;        // cached boundary string
    bool           isComplete = false;  // |turn sum| == 4
    bool           pruned     = false;  // Algorithm 1: skip in matching
    std::vector<int> parentIds;         // source nodes glued to produce this node
};

// ============================================================
// MerrellGrammar
// ============================================================
class MerrellGrammar {
public:
    // ---- MG-1: Input -------------------------------------------------------

    // Grid-phase: build MerrellGraph primitives from tile vocabulary.
    // socketDefs: one per unique tile label with its socket directions.
    // tiles: all placed instances (only labels are used for primitives;
    //        cell/rotation are ignored until scene integration at MG-5).
    void loadFromTiles(const std::vector<TileSocketDef>& socketDefs,
                       const std::vector<TileInput>&     tiles);

    // Free-form mesh disassembly (MG-5/MG-6). Takes pointer so forward
    // declaration of MeshData is sufficient. NOT IMPLEMENTED until MG-5.
    void loadFromShape(const MeshData* mesh);

    // ---- MG-3: Grammar extraction ------------------------------------------

    // Run Algorithm 1. Fills m_rules.
    // progressCb(currentStep, totalSteps) for UI progress bar.
    void extractGrammar(std::function<void(int,int)> progressCb = {});

    // ---- MG-4: Shape generation --------------------------------------------

    // Run Algorithm 3. Fills m_result.
    void generate(int seed = 42,
                  std::function<void(int,int)> progressCb = {});

    // Step-based for animated preview. Call beginGenerate once, then
    // stepGenerate each frame until it returns true.
    void beginGenerate(int seed = 42);
    bool stepGenerate();

    // ---- MG-5: Serialisation -----------------------------------------------
    bool saveRules(const std::string& path) const;
    bool loadRules(const std::string& path);

    // ---- Results & accessors -----------------------------------------------
    const GenerationResult&           result()     const { return m_result; }
    const std::vector<DPORule>&       rules()      const { return m_rules; }
    const std::vector<HierarchyNode>& hierarchy()  const { return m_hierarchy; }
    const std::vector<MerrellGraph>&  primitives() const { return m_primitives; }
    GrammarSettings&                  settings()         { return m_settings; }
    const GrammarSettings&            settings()   const { return m_settings; }

    bool hasRules()  const { return !m_rules.empty(); }
    bool hasResult() const { return m_result.success; }

    int primitiveCount()  const { return (int)m_primitives.size(); }
    int ruleCount()       const { return (int)m_rules.size(); }
    int hierarchyDepth()  const;

    const std::string& lastError() const { return m_lastError; }

private:
    GrammarSettings            m_settings;
    std::vector<MerrellGraph>  m_primitives;
    std::vector<HierarchyNode> m_hierarchy;
    std::vector<DPORule>       m_rules;
    GenerationResult           m_result;
    std::string                m_lastError;

    // Step state for animated generation
    MerrellGraph  m_genState;
    int           m_genSeed  = 42;
    int           m_genStep  = 0;
    bool          m_genDone  = false;

    void algorithm1_findGrammar(std::function<void(int,int)> progressCb);
    bool algorithm2_findMatchingGroups(int hierarchyNodeId);
    bool algorithm3_step();

    void buildHierarchy(std::function<void(int,int)> progressCb = {});
    void tryLoopGluings(int generation);
    void tryBranchGluings(int generation);

    bool      solvePositions(MerrellGraph& graph);
    RuleMatch findMatch(const DPORule& rule, const MerrellGraph& G, int seed) const;
    bool      applyRule(const DPORule& rule, const RuleMatch& match, MerrellGraph& G);

    int nextHierarchyId() const { return (int)m_hierarchy.size(); }
    int nextRuleId()      const { return (int)m_rules.size(); }
};

} // namespace merrell

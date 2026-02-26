#pragma once
// GraphViewer — MG-2.5 imnodes-based visual inspector for the Merrell grammar.
//
// Provides a tabbed panel with three views:
//   Tab 1  PRIMITIVES  — MG-1 output: one node per tile primitive.
//   Tab 2  HIERARCHY   — MG-2 output: DAG of glued graphs by generation.
//   Tab 3  RULES       — MG-3 output: extracted DPO rule set.
//
// Each view displays all five core inspectable types:
//   HalfEdge       directed half-edge with label (l, r, theta)
//   MerrellGraph   vertices + half-edges + face labels
//   BoundaryString circular sequence of edges and turns
//   DPORule        L / I / R graphs with phi_L and phi_R morphisms
//   GrammarRuleSet vector<DPORule> with starter rules pinned at top
//
// Usage:
//   GraphViewer viewer;
//   viewer.setGrammar(&m_merrell);         // call once at init
//   viewer.drawPanel(uiState);             // call from App::render()
//
// Reads MerrellGrammar as const — never modifies grammar state.
// Registered as a panel in App::render() under GRAPH_GRAMMAR mode.

#include "../grammar-core/MerrellGrammar.h"
#include "../../src/EditorUI.h"
#include <imnodes.h>

class GraphViewer
{
public:
    // Point at the live MerrellGrammar instance. Must be called before drawPanel.
    void setGrammar(const merrell::MerrellGrammar* grammar) { m_grammar = grammar; }

    // Draw the panel. Only draws when state.mode == GRAPH_GRAMMAR.
    // Respects state.panelsHidden.
    void drawPanel(EditorUIState& state);

    bool isOpen()           const { return m_open; }
    void setOpen(bool open)       { m_open = open; }

private:
    const merrell::MerrellGrammar* m_grammar = nullptr;
    bool m_open       = true;
    int  m_activeTab  = 0;   // 0=Primitives  1=Hierarchy  2=Rules
    int  m_selectedPrimitive  = -1;  // index into primitives() for detail view
    int  m_selectedHierNode   = -1;  // index into hierarchy() for detail view
    int  m_selectedRule       = -1;  // index into rules() for detail view

    // imnodes context — created once, owned here.
    // Forward-declared as struct so GraphViewer.h compiles without imnodes.h.
    // (imnodes supports multiple contexts for multiple canvases.)
    ImNodesContext* m_ctx = nullptr;

    void ensureContext();
    void destroyContext();

    // ---- Tab drawing -------------------------------------------------------
    void drawPrimitivesTab();
    void drawHierarchyTab();
    void drawRulesTab();

    // ---- Per-type renderers ------------------------------------------------
    // Draw a MerrellGraph as imnodes nodes.
    // nodeIdBase: starting imnodes node id (to avoid collisions between graphs).
    // Returns the next free node id after this graph's nodes.
    int drawGraph(const merrell::MerrellGraph& graph,
                  int nodeIdBase,
                  float offsetX = 0.f, float offsetY = 0.f);

    // Draw a BoundaryString as a horizontal chain of labelled boxes.
    void drawBoundaryString(const merrell::BoundaryString& bs, const char* label);

    // Draw a DPORule showing L / I / R side by side.
    void drawDPORule(const merrell::DPORule& rule, int nodeIdBase);

    // Colour helpers
    static ImU32 halfEdgeColour(const merrell::EdgeLabel& lbl);
    static ImU32 faceColour(const std::string& label);
};

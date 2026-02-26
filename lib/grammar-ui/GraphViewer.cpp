#include "GraphViewer.h"
#include <imgui.h>
#include <imnodes.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ============================================================
// Colour palette
// ============================================================
// Edge label r value → colour for the half-edge line
static const ImU32 kColOpen     = IM_COL32(80,  200, 180, 255);  // teal  = socket (open)
static const ImU32 kColExterior = IM_COL32(100, 100, 120, 255);  // grey  = sealed
static const ImU32 kColCut      = IM_COL32(220, 80,  80,  255);  // red   = twin==-1
static const ImU32 kColDefault  = IM_COL32(160, 160, 200, 255);  // blue-grey = other

// Turn symbol colours
static const ImU32 kColTurnPos  = IM_COL32(100, 220, 100, 255);  // green = positive ^
static const ImU32 kColTurnNeg  = IM_COL32(220, 120, 60,  255);  // orange = negative v

// Face colours — cycle through a small palette keyed by first char of label
static const ImU32 kFacePalette[] = {
    IM_COL32(50, 90, 160, 200),   // blue
    IM_COL32(50, 140, 80, 200),   // green
    IM_COL32(160, 100, 50, 200),  // orange
    IM_COL32(140, 50, 130, 200),  // purple
    IM_COL32(50, 130, 150, 200),  // teal
    IM_COL32(150, 140, 50, 200),  // yellow-green
};

ImU32 GraphViewer::halfEdgeColour(const merrell::EdgeLabel& lbl)
{
    if (lbl.r == "open")     return kColOpen;
    if (lbl.r == "exterior") return kColExterior;
    return kColDefault;
}

ImU32 GraphViewer::faceColour(const std::string& label)
{
    if (label.empty()) return IM_COL32(80, 80, 100, 200);
    int idx = (int)(label[0]) % (int)(sizeof(kFacePalette)/sizeof(kFacePalette[0]));
    return kFacePalette[idx];
}

// ============================================================
// imnodes context lifecycle
// ============================================================
void GraphViewer::ensureContext()
{
    if (!m_ctx)
        m_ctx = ImNodes::CreateContext();
}

void GraphViewer::destroyContext()
{
    if (m_ctx) {
        ImNodes::DestroyContext(m_ctx);
        m_ctx = nullptr;
    }
}

// ============================================================
// Main panel entry point
// ============================================================
void GraphViewer::drawPanel(EditorUIState& state)
{
    if (!m_open) return;
    if (state.mode != EditorMode::GRAPH_GRAMMAR) return;
    if (state.panelsHidden) return;

    ensureContext();

    // Position to the right of the scene panel, left of the right edge
    ImGuiIO& io = ImGui::GetIO();
    float topY  = state.menuBarHeight + state.toolbarHeight;
    float botY  = io.DisplaySize.y - state.statusBarHeight;
    float leftX = state.scenePanelWidth;  // start where scene panel ends
    float width = io.DisplaySize.x - leftX;
    float height = botY - topY;

    ImGui::SetNextWindowPos ({leftX, topY});
    ImGui::SetNextWindowSize({width, height});
    ImGui::SetNextWindowBgAlpha(1.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.f, 6.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.12f, 0.13f, 0.16f, 1.f});

    ImGui::Begin("##graphviewer", nullptr,
        ImGuiWindowFlags_NoDecoration    |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- Header ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.6f, 0.8f, 1.f, 1.f});
    ImGui::Text("GRAPH VIEWER");
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (!m_grammar) {
        ImGui::SameLine();
        ImGui::TextDisabled("(no grammar loaded)");
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        return;
    }

    // Stats badge
    char badge[128];
    snprintf(badge, sizeof(badge), "  %d prims  |  %d hier nodes  |  %d rules",
        m_grammar->primitiveCount(),
        (int)m_grammar->hierarchy().size(),
        m_grammar->ruleCount());
    ImGui::TextDisabled("%s", badge);
    ImGui::Separator();

    // ---- Tab bar ----
    static const char* kTabNames[] = { "Primitives (MG-1)", "Hierarchy (MG-2)", "Rules (MG-3)" };
    ImGui::PushStyleColor(ImGuiCol_Tab,        ImVec4{0.16f, 0.18f, 0.24f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_TabActive,  ImVec4{0.22f, 0.40f, 0.65f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4{0.28f, 0.50f, 0.80f, 1.f});

    if (ImGui::BeginTabBar("##gv_tabs")) {
        for (int t = 0; t < 3; ++t) {
            bool open = ImGui::BeginTabItem(kTabNames[t]);
            if (ImGui::IsItemClicked()) m_activeTab = t;
            if (open) {
                m_activeTab = t;
                switch (t) {
                    case 0: drawPrimitivesTab(); break;
                    case 1: drawHierarchyTab();  break;
                    case 2: drawRulesTab();       break;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(3);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ============================================================
// Tab 1 — Primitives
// ============================================================
void GraphViewer::drawPrimitivesTab()
{
    if (!m_grammar) return;
    const auto& prims = m_grammar->primitives();

    if (prims.empty()) {
        ImGui::TextDisabled("No primitives yet. Grammar not loaded.");
        ImGui::TextDisabled("Call loadFromTiles() and extractGrammar() first.");
        return;
    }

    // Left pane: primitive list
    float listW = 180.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
    ImGui::BeginChild("##prim_list", {listW, -1.f}, true);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
    ImGui::Text("TILE PRIMITIVES");
    ImGui::PopStyleColor();
    ImGui::Separator();

    for (int i = 0; i < (int)prims.size(); ++i) {
        const auto& prim = prims[i];
        std::string faceLbl = prim.faces.empty() ? "(empty)" : prim.faces[0].label;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s##primsel_%d", faceLbl.c_str(), i);

        bool sel = (m_selectedPrimitive == i);
        ImGui::PushStyleColor(ImGuiCol_Header,
            sel ? ImVec4{0.22f,0.40f,0.72f,0.8f} : ImVec4{0,0,0,0});

        if (ImGui::Selectable(buf, sel))
            m_selectedPrimitive = i;

        ImGui::PopStyleColor();

        // Quick stats inline
        ImGui::SameLine();
        ImGui::TextDisabled("v%d e%d", prim.vertexCount(), prim.edgeCount());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Right pane: selected primitive detail
    ImGui::BeginChild("##prim_detail", {-1.f, -1.f}, false);

    if (m_selectedPrimitive >= 0 && m_selectedPrimitive < (int)prims.size()) {
        const auto& prim = prims[m_selectedPrimitive];

        // ---- Text detail section ----
        float detailH = 200.f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
        ImGui::BeginChild("##prim_text", {-1.f, detailH}, true);

        if (!prim.faces.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
            ImGui::Text("FACE: %s", prim.faces[0].label.c_str());
            ImGui::PopStyleColor();

            // Boundary string
            merrell::BoundaryString bs = prim.boundaryOf(prim.faces[0].id);
            ImGui::TextDisabled("boundary: ");
            ImGui::SameLine();

            // Render boundary string inline with colour coding
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.f, 0.f});
            for (const auto& el : bs.elements) {
                if (el.is_turn) {
                    if (el.turn_type == merrell::TurnType::Positive)
                        ImGui::TextColored({0.4f,0.9f,0.4f,1.f}, "^");
                    else
                        ImGui::TextColored({0.9f,0.5f,0.2f,1.f}, "v");
                } else {
                    // Find the half-edge to get its label
                    const auto* he = prim.halfEdge(el.edge_id);
                    if (he) {
                        ImVec4 col;
                        if (he->label.r == "open")
                            col = {0.3f, 0.8f, 0.7f, 1.f};
                        else
                            col = {0.6f, 0.6f, 0.7f, 1.f};
                        ImGui::TextColored(col, "E%d", el.edge_id);
                    }
                }
                ImGui::SameLine();
            }
            ImGui::PopStyleVar();
            ImGui::NewLine();

            ImGui::Text("turns=%d  complete=%s",
                bs.totalTurnCount(), bs.isComplete() ? "YES" : "NO");
        }

        ImGui::Separator();

        // Half-edge table
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
        ImGui::Text("HALF-EDGES");
        ImGui::PopStyleColor();

        if (ImGui::BeginTable("##he_table", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_ScrollY, {-1.f, 0.f}))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("V0→V1");
            ImGui::TableSetupColumn("l");
            ImGui::TableSetupColumn("r");
            ImGui::TableSetupColumn("θ°");
            ImGui::TableHeadersRow();

            for (size_t i = 0; i + 1 < prim.halfEdges.size(); i += 2) {
                const auto& he   = prim.halfEdges[i];
                const auto& twin = prim.halfEdges[i + 1];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImVec4 rowCol = (he.label.r == "open")
                    ? ImVec4{0.3f,0.8f,0.7f,1.f}
                    : ImVec4{0.65f,0.65f,0.75f,1.f};
                ImGui::PushStyleColor(ImGuiCol_Text, rowCol);

                ImGui::Text("HE%d", he.id);
                ImGui::TableNextColumn();
                ImGui::Text("V%d→V%d", he.vertex, twin.vertex);
                ImGui::TableNextColumn();
                ImGui::Text("%s", he.label.l.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", he.label.r.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", he.label.theta * 180.f / 3.14159f);

                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // ---- imnodes canvas ----
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
        ImGui::Text("GRAPH CANVAS");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.08f, 0.09f, 0.12f, 1.f});
        ImGui::BeginChild("##prim_canvas", {-1.f, -1.f}, true);

        ImNodes::SetCurrentContext(m_ctx);
        ImNodes::BeginNodeEditor();

        drawGraph(prim, m_selectedPrimitive * 1000, 50.f, 50.f);

        ImNodes::MiniMap(0.12f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        ImGui::EndChild();
        ImGui::PopStyleColor();

    } else {
        ImGui::TextDisabled("Select a primitive from the list.");
    }

    ImGui::EndChild();
}

// ============================================================
// Tab 2 — Hierarchy
// ============================================================
void GraphViewer::drawHierarchyTab()
{
    if (!m_grammar) return;
    const auto& hier = m_grammar->hierarchy();

    if (hier.empty()) {
        ImGui::TextDisabled("Hierarchy not built yet.");
        ImGui::TextDisabled("Call extractGrammar() to build the hierarchy.");
        return;
    }

    // Left pane: hierarchy node list, grouped by generation
    float listW = 220.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
    ImGui::BeginChild("##hier_list", {listW, -1.f}, true);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
    ImGui::Text("HIERARCHY  (%d nodes)", (int)hier.size());
    ImGui::PopStyleColor();
    ImGui::Separator();

    int curGen = -1;
    for (int i = 0; i < (int)hier.size(); ++i) {
        const auto& node = hier[i];

        // Generation header
        if (node.generation != curGen) {
            curGen = node.generation;
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.5f, 0.7f, 1.f, 1.f});
            ImGui::Text("Gen %d", curGen);
            ImGui::PopStyleColor();
        }

        // Node entry
        char buf[128];
        snprintf(buf, sizeof(buf), "  N%d##hiersel_%d", node.id, i);

        bool sel = (m_selectedHierNode == i);
        ImGui::PushStyleColor(ImGuiCol_Header,
            sel ? ImVec4{0.22f,0.40f,0.72f,0.8f} : ImVec4{0,0,0,0});

        if (ImGui::Selectable(buf, sel))
            m_selectedHierNode = i;

        ImGui::PopStyleColor();

        // Badges inline
        ImGui::SameLine();
        if (node.isComplete)
            ImGui::TextColored({0.3f,0.9f,0.3f,1.f}, "●");  // complete
        else
            ImGui::TextColored({0.5f,0.5f,0.5f,1.f}, "○");  // incomplete
        if (node.pruned) {
            ImGui::SameLine();
            ImGui::TextColored({0.8f,0.4f,0.3f,1.f}, "✕");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("f%d", node.graph.faceCount());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Right pane: selected node detail
    ImGui::BeginChild("##hier_detail", {-1.f, -1.f}, false);

    if (m_selectedHierNode >= 0 && m_selectedHierNode < (int)hier.size()) {
        const auto& node = hier[m_selectedHierNode];

        float detailH = 160.f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
        ImGui::BeginChild("##hier_text", {-1.f, detailH}, true);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
        ImGui::Text("NODE %d  —  Gen %d", node.id, node.generation);
        ImGui::PopStyleColor();

        ImGui::Text("faces=%d  edges=%d  verts=%d",
            node.graph.faceCount(), node.graph.edgeCount(), node.graph.vertexCount());

        ImGui::Text("complete: %s  pruned: %s",
            node.isComplete ? "YES" : "NO",
            node.pruned     ? "YES" : "NO");

        // Boundary string
        if (!node.boundary.isEmpty()) {
            ImGui::TextDisabled("boundary: %s  (turns=%d)",
                node.boundary.toString().c_str(),
                node.boundary.totalTurnCount());
        } else {
            ImGui::TextDisabled("boundary: (empty)");
        }

        // Parents
        if (!node.childIds.empty()) {
            ImGui::TextDisabled("children:");
            ImGui::SameLine();
            for (int cid : node.childIds) {
                ImGui::SameLine();
                ImGui::TextDisabled("N%d", cid);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Canvas
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.08f, 0.09f, 0.12f, 1.f});
        ImGui::BeginChild("##hier_canvas", {-1.f, -1.f}, true);

        ImNodes::SetCurrentContext(m_ctx);
        ImNodes::BeginNodeEditor();

        drawGraph(node.graph, 2000 + m_selectedHierNode * 200, 50.f, 50.f);

        ImNodes::MiniMap(0.12f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        ImGui::EndChild();
        ImGui::PopStyleColor();

    } else {
        ImGui::TextDisabled("Select a hierarchy node from the list.");
    }

    ImGui::EndChild();
}

// ============================================================
// Tab 3 — Rules
// ============================================================
void GraphViewer::drawRulesTab()
{
    if (!m_grammar) return;
    const auto& rules = m_grammar->rules();

    if (rules.empty()) {
        ImGui::TextDisabled("No rules extracted yet.");
        ImGui::TextDisabled("Call extractGrammar() to build the rule set.");
        return;
    }

    float listW = 220.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
    ImGui::BeginChild("##rule_list", {listW, -1.f}, true);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
    ImGui::Text("RULES  (%d)", (int)rules.size());
    ImGui::PopStyleColor();
    ImGui::Separator();

    for (int i = 0; i < (int)rules.size(); ++i) {
        const auto& rule = rules[i];

        // Kind badge
        const char* kindStr = "?";
        ImVec4 kindCol = {0.7f,0.7f,0.7f,1.f};
        switch (rule.kind) {
            case merrell::RuleKind::Starter:    kindStr="S";  kindCol={0.9f,0.8f,0.3f,1.f}; break;
            case merrell::RuleKind::LoopGlue:   kindStr="L";  kindCol={0.3f,0.8f,0.7f,1.f}; break;
            case merrell::RuleKind::BranchGlue: kindStr="B";  kindCol={0.8f,0.5f,0.3f,1.f}; break;
            case merrell::RuleKind::Stub:        kindStr="Sb"; kindCol={0.6f,0.5f,0.8f,1.f}; break;
            case merrell::RuleKind::General:    kindStr="G";  kindCol={0.7f,0.7f,0.9f,1.f}; break;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "[%s] %s##rulesel_%d",
            kindStr, rule.name.empty() ? "(unnamed)" : rule.name.c_str(), i);

        bool sel = (m_selectedRule == i);
        ImGui::PushStyleColor(ImGuiCol_Header,
            sel ? ImVec4{0.22f,0.40f,0.72f,0.8f} : ImVec4{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_Text, kindCol);
        if (ImGui::Selectable(buf, sel)) m_selectedRule = i;
        ImGui::PopStyleColor(2);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    // Rule detail pane
    ImGui::BeginChild("##rule_detail", {-1.f, -1.f}, false);

    if (m_selectedRule >= 0 && m_selectedRule < (int)rules.size()) {
        const auto& rule = rules[m_selectedRule];

        float detailH = 120.f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.13f, 1.f});
        ImGui::BeginChild("##rule_text", {-1.f, detailH}, true);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.9f, 1.f});
        ImGui::Text("RULE %d: %s", rule.id, rule.name.c_str());
        ImGui::PopStyleColor();

        ImGui::Text("L: %d faces  R: %d faces  I: %d faces",
            rule.L.faceCount(), rule.R.faceCount(), rule.I.faceCount());

        if (!rule.boundary_L.isEmpty())
            ImGui::TextDisabled("∂L: %s", rule.boundary_L.toString().c_str());
        if (!rule.boundary_R.isEmpty())
            ImGui::TextDisabled("∂R: %s", rule.boundary_R.toString().c_str());

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Canvas — show L / I / R graphs side by side
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.08f, 0.09f, 0.12f, 1.f});
        ImGui::BeginChild("##rule_canvas", {-1.f, -1.f}, true);

        ImNodes::SetCurrentContext(m_ctx);
        ImNodes::BeginNodeEditor();

        drawDPORule(rule, 5000 + m_selectedRule * 500);

        ImNodes::MiniMap(0.12f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Select a rule from the list.");
    }

    ImGui::EndChild();
}

// ============================================================
// drawGraph — render a MerrellGraph as imnodes
// ============================================================
int GraphViewer::drawGraph(const merrell::MerrellGraph& graph,
                           int nodeIdBase,
                           float offsetX, float offsetY)
{
    if (graph.isEmpty()) return nodeIdBase;

    // Layout: place vertex nodes at their stored positions (scaled to canvas coords)
    // Face nodes in the center of their boundary vertices
    // Edge node per half-edge pair connecting the two vertex nodes

    static const float kScale = 120.f;  // world units → canvas pixels
    int nextId = nodeIdBase;

    // ---- Vertex nodes ----
    for (const auto& v : graph.vertices) {
        int nodeId = nextId++;
        ImNodes::SetNodeEditorSpacePos(nodeId,
            ImVec2{offsetX + v.pos.x * kScale, offsetY + (1.f - v.pos.y) * kScale});

        ImNodes::BeginNode(nodeId);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("V");
        ImGui::SameLine();
        ImGui::Text("%d", v.id);
        ImNodes::EndNodeTitleBar();

        // Output pin
        ImNodes::BeginOutputAttribute(nextId++);
        ImGui::TextDisabled("(%.2f,%.2f)", v.pos.x, v.pos.y);
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();
    }

    // ---- Face nodes ----
    for (const auto& f : graph.faces) {
        int nodeId = nextId++;

        // Position: compute centroid of face boundary vertices
        float cx = 0.f, cy = 0.f;
        int count = 0;
        if (f.start_he != -1) {
            int cur = f.start_he, safety = 0;
            do {
                const auto* he = graph.halfEdge(cur);
                if (!he || ++safety > 100) break;
                const auto* v = graph.vertex(he->vertex);
                if (v) { cx += v->pos.x; cy += v->pos.y; ++count; }
                cur = he->next;
            } while (cur != f.start_he);
        }
        if (count > 0) { cx /= count; cy /= count; }

        ImNodes::SetNodeEditorSpacePos(nodeId,
            ImVec2{offsetX + cx * kScale + 60.f,
                   offsetY + (1.f - cy) * kScale + 60.f});

        ImU32 titleCol = faceColour(f.label);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar,         titleCol);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  titleCol);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, titleCol);

        ImNodes::BeginNode(nodeId);
        ImNodes::BeginNodeTitleBar();
        ImGui::Text("F%d: %s", f.id, f.label.c_str());
        ImNodes::EndNodeTitleBar();
        ImGui::TextDisabled("deg=%d", f.degree);

        // Boundary string
        merrell::BoundaryString bs = graph.boundaryOf(f.id);
        if (!bs.isEmpty()) {
            ImGui::TextDisabled("bnd: %s", bs.toString().c_str());
            bool complete = bs.isComplete();
            if (complete)
                ImGui::TextColored({0.3f,0.9f,0.3f,1.f}, "● complete");
            else
                ImGui::TextColored({0.6f,0.6f,0.6f,1.f}, "○ open");
        }

        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // ---- Link: half-edge pairs as labelled links ----
    // We can't draw directional arrows with imnodes (it only draws bezier links
    // between pins). Instead draw text annotation nodes for each edge pair.
    // Simple approach: one node per edge pair, positioned at edge midpoint.
    for (size_t i = 0; i + 1 < graph.halfEdges.size(); i += 2) {
        const auto& he   = graph.halfEdges[i];
        const auto& twin = graph.halfEdges[i + 1];

        // Find endpoint positions
        const auto* v0 = graph.vertex(he.vertex);
        const auto* v1 = graph.vertex(twin.vertex);
        if (!v0 || !v1) continue;

        float mx = (v0->pos.x + v1->pos.x) * 0.5f;
        float my = (v0->pos.y + v1->pos.y) * 0.5f;

        int nodeId = nextId++;
        ImNodes::SetNodeEditorSpacePos(nodeId,
            ImVec2{offsetX + mx * kScale,
                   offsetY + (1.f - my) * kScale + 30.f});

        ImU32 edgeCol = halfEdgeColour(he.label);
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground,         IM_COL32(20,22,30,220));
        ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered,  IM_COL32(30,34,45,240));
        ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(40,60,90,255));

        ImNodes::BeginNode(nodeId);
        ImNodes::BeginNodeTitleBar();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{
            ((edgeCol >>  0) & 0xFF) / 255.f,
            ((edgeCol >>  8) & 0xFF) / 255.f,
            ((edgeCol >> 16) & 0xFF) / 255.f, 1.f});
        ImGui::Text("E%zu", i/2);
        ImGui::PopStyleColor();

        ImNodes::EndNodeTitleBar();
        ImGui::TextDisabled("l:%s r:%s", he.label.l.c_str(), he.label.r.c_str());
        ImGui::TextDisabled("θ=%.0f°", he.label.theta * 180.f / 3.14159f);

        if (he.twin == -1)
            ImGui::TextColored({0.9f,0.3f,0.3f,1.f}, "cut");
        else
            ImGui::TextDisabled("twin=HE%d", he.twin);

        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    return nextId;
}

// ============================================================
// drawBoundaryString — horizontal inline chain
// ============================================================
void GraphViewer::drawBoundaryString(const merrell::BoundaryString& bs,
                                     const char* label)
{
    if (label) {
        ImGui::TextDisabled("%s: ", label);
        ImGui::SameLine();
    }

    if (bs.isEmpty()) {
        ImGui::TextDisabled("(empty)");
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.f, 0.f});
    for (const auto& el : bs.elements) {
        if (el.is_turn) {
            if (el.turn_type == merrell::TurnType::Positive)
                ImGui::TextColored({0.4f,0.9f,0.4f,1.f}, "^");
            else
                ImGui::TextColored({0.9f,0.5f,0.2f,1.f}, "v");
        } else {
            ImGui::TextColored({0.5f,0.7f,0.9f,1.f}, "E%d", el.edge_id);
        }
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::NewLine();
}

// ============================================================
// drawDPORule — L / I / R side by side
// ============================================================
void GraphViewer::drawDPORule(const merrell::DPORule& rule, int nodeIdBase)
{
    int nextId = nodeIdBase;

    // Column layout: L at x=0, I at x=400, R at x=800
    nextId = drawGraph(rule.L, nextId,  30.f, 60.f);
    nextId = drawGraph(rule.I, nextId, 430.f, 60.f);
    nextId = drawGraph(rule.R, nextId, 830.f, 60.f);

    // Label columns
    // (imnodes doesn't support static text nodes — draw as minimal nodes)
    auto drawLabel = [&](const char* text, float x, float y) {
        int nid = nextId++;
        ImNodes::SetNodeEditorSpacePos(nid, ImVec2{x, y});
        ImNodes::PushColorStyle(ImNodesCol_NodeBackground,        IM_COL32(0,0,0,0));
        ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(0,0,0,0));
        ImNodes::PushColorStyle(ImNodesCol_NodeOutline,           IM_COL32(0,0,0,0));
        ImNodes::BeginNode(nid);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.6f,0.8f,1.f,1.f});
        ImGui::Text("%s", text);
        ImGui::PopStyleColor();
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    };

    drawLabel("L (left)", 30.f, 20.f);
    drawLabel("I (interface)", 430.f, 20.f);
    drawLabel("R (right)", 830.f, 20.f);

    // Draw phi_L / phi_R morphism hints as text inside the canvas
    // (full morphism links would require matching pin IDs across graphs —
    //  deferred to MG-3 when morphisms are actually populated)
    int mNode = nextId++;
    ImNodes::SetNodeEditorSpacePos(mNode, ImVec2{380.f, 200.f});
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(20,24,35,220));
    ImNodes::BeginNode(mNode);
    ImGui::TextColored({0.7f,0.85f,1.f,1.f}, "φL: I→L");
    ImGui::TextColored({0.7f,0.85f,1.f,1.f}, "φR: I→R");
    ImGui::TextDisabled("(morphism links");
    ImGui::TextDisabled(" shown in MG-3)");
    ImNodes::EndNode();
    ImNodes::PopColorStyle();
}

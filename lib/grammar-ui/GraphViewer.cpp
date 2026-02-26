#include "GraphViewer.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

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
// imnodes context lifecycle (kept for API compat; imnodes no longer used
// for graph rendering — we draw directly via ImDrawList instead)
// ============================================================
void GraphViewer::ensureContext()  { /* no-op */ }
void GraphViewer::destroyContext() { /* no-op */ }

// ============================================================
// Main panel entry point
// ============================================================
void GraphViewer::drawPanel(EditorUIState& state)
{
    if (!m_open) return;
    if (state.mode != EditorMode::GRAPH_GRAMMAR) return;
    if (state.panelsHidden) return;


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

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.06f, 0.07f, 0.10f, 1.f});
        ImGui::BeginChild("##prim_canvas", {-1.f, -1.f}, true);

        // Reserve space for drawing and call drawGraph directly into draw list
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(avail);  // claim the space so clipping rect is set

        // Draw the graph (ImDrawList-based, no imnodes nodes)
        drawGraph(prim, m_selectedPrimitive * 1000, 40.f, 40.f);

        // Legend
        ImDrawList* leg = ImGui::GetWindowDrawList();
        ImVec2 lo = ImGui::GetWindowPos();
        float lx = lo.x + 10.f, ly = lo.y + avail.y - 54.f;
        leg->AddRectFilled({lx-4, ly-4}, {lx+180, ly+52}, IM_COL32(15,18,28,210), 4.f);
        leg->AddCircleFilled({lx+8, ly+8},  5, IM_COL32(180,200,240,200));
        leg->AddText({lx+18, ly+1}, IM_COL32(180,200,240,255), "Vertex");
        leg->AddLine({lx+4, ly+22}, {lx+30, ly+22}, kColOpen, 2.2f);
        leg->AddText({lx+36, ly+15}, kColOpen, "Open edge");
        leg->AddLine({lx+4, ly+38}, {lx+30, ly+38}, kColExterior, 1.4f);
        leg->AddText({lx+36, ly+31}, kColExterior, "Exterior edge");

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
        if (!node.parentIds.empty()) {
            ImGui::TextDisabled("parents:");
            ImGui::SameLine();
            for (int cid : node.parentIds) {
                ImGui::SameLine();
                ImGui::TextDisabled("N%d", cid);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Canvas
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.06f, 0.07f, 0.10f, 1.f});
        ImGui::BeginChild("##hier_canvas", {-1.f, -1.f}, true);

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(avail);

        drawGraph(node.graph, 2000 + m_selectedHierNode * 200, 40.f, 40.f);

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
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.06f, 0.07f, 0.10f, 1.f});
        ImGui::BeginChild("##rule_canvas", {-1.f, -1.f}, true);

        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(avail);

        // L / I / R in three horizontal columns
        // Each column is avail.x/3 wide; use that as the column step.
        float colW = avail.x / 3.f;
        float graphH = avail.y;
        // Scale: fit a ~2-unit wide graph into colW with some margin
        // kScale in drawGraph is 140px, so a 1-unit tile = 140px.
        // For multi-tile graphs (L) we may need to zoom out — drawGraph uses
        // fixed scale, so just pass appropriate offsets and let user see what fits.

        // Draw column separator lines
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        ImVec2 cpos = ImGui::GetWindowPos();
        cdl->AddLine({cpos.x + colW,     cpos.y}, {cpos.x + colW,     cpos.y + graphH}, IM_COL32(60,70,100,180), 1.f);
        cdl->AddLine({cpos.x + colW*2.f, cpos.y}, {cpos.x + colW*2.f, cpos.y + graphH}, IM_COL32(60,70,100,180), 1.f);

        // Column header labels
        auto colLabel = [&](const char* text, float cx, float col) {
            cdl->AddText({cpos.x + col + 8.f, cpos.y + 6.f},
                         IM_COL32(140, 180, 255, 230), text);
        };
        colLabel("L  (result)",    colW,   0.f);
        colLabel("I  (interface)", colW*2, colW);
        colLabel("R  (matched)",   colW*3, colW*2);

        // Draw the three graphs at their column offsets
        // We override offsetX so each graph starts at the right column.
        // Because drawGraph uses canvasOrigin = GetCursorScreenPos() and our
        // Dummy() already moved cursor past the available region, we need
        // to temporarily set cursor back to child top so the worldToScreen
        // lambda picks up the right origin. Easiest: pass column offset in
        // offsetX as pixels from the child's top-left.
        // offsetY = 30 to leave room for column header
        drawGraph(rule.L, 5000 + m_selectedRule * 500,          30.f, 30.f);

        // For I and R, shift by column offset pixels.
        // drawGraph adds offsetX to canvasOrigin.x, so this works.
        // But canvasOrigin comes from GetCursorScreenPos which is now at
        // child bottom after Dummy. We need to reset it or use absolute coords.
        // Workaround: use negative offsetX correction relative to cpos.
        // The child's cursor after Dummy() has y = cpos.y + avail.y.
        // worldToScreen: screen.x = GetCursorScreenPos().x + offsetX + wx*scale
        // We want screen.x = cpos.x + colWidth + wx*scale + margin
        // So offsetX for column 1 = colWidth + margin - (GetCursorScreenPos().x - cpos.x)
        // Since cursor moved to end of Dummy, GetCursorScreenPos.x = cpos.x
        // (cursor x resets to left margin after Dummy in a vertical layout)
        // So: offsetX_for_col1 = colW + 30
        //     offsetX_for_col2 = colW*2 + 30
        drawGraph(rule.I, 5000 + m_selectedRule * 500 + 100,   colW + 30.f,   30.f);
        drawGraph(rule.R, 5000 + m_selectedRule * 500 + 200,   colW*2 + 30.f, 30.f);

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Select a rule from the list.");
    }

    ImGui::EndChild();
}

// ============================================================
// drawGraph — render a MerrellGraph directly via ImDrawList
// ============================================================
// Instead of imnodes nodes (which caused overlap and illegibility),
// we draw directly onto the canvas using ImDrawList:
//   • Vertices    = filled circles with ID label
//   • Edges       = directed arrows coloured by r-label, with θ/label text
//   • Face labels = centroid text
// The canvas itself is still an imnodes editor (for pan/zoom), but we
// draw into its background draw-list so nothing is draggable.
// ─────────────────────────────────────────────────────────────────────────────
int GraphViewer::drawGraph(const merrell::MerrellGraph& graph,
                           int /*nodeIdBase — unused, kept for API compat*/,
                           float offsetX, float offsetY)
{
    if (graph.isEmpty()) return 0;

    // ── Layout constants ────────────────────────────────────────────────────
    // Scale world-space coords (0–2 range typical) to canvas pixels.
    // kScale chosen so a single 1×1 tile fills ~140 px.
    static const float kScale    = 140.f;
    static const float kVtxR     = 7.f;   // vertex circle radius
    static const float kArrowLen = 10.f;  // arrowhead leg length
    static const float kArrowAng = 0.45f; // arrowhead half-angle (radians)

    // Edge label text offset (pixels, perpendicular to edge direction)
    static const float kLabelOff = 14.f;

    // Get draw list; draw into the background so items are non-interactive
    // but still clipped to the canvas region.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();

    auto worldToScreen = [&](float wx, float wy) -> ImVec2 {
        // Flip Y: world y=0 at bottom, screen y=0 at top.
        return ImVec2{
            canvasOrigin.x + offsetX + wx * kScale,
            canvasOrigin.y + offsetY + (2.f - wy) * kScale
        };
    };

    // ── Draw faces (filled polygons as background) ───────────────────────────
    for (const auto& f : graph.faces) {
        if (f.start_he == -1) continue;

        // Collect face boundary vertex positions
        std::vector<ImVec2> poly;
        int cur = f.start_he, safety = 0;
        do {
            const auto* he = graph.halfEdge(cur);
            if (!he || ++safety > 200) break;
            const auto* v = graph.vertex(he->vertex);
            if (v) poly.push_back(worldToScreen(v->pos.x, v->pos.y));
            cur = he->next;
        } while (cur != f.start_he && safety < 200);

        if (poly.size() >= 3) {
            ImU32 fillCol = faceColour(f.label);
            // Make it semi-transparent so edges/labels show through
            fillCol = (fillCol & 0x00FFFFFF) | 0x28000000;  // alpha ~16%
            dl->AddConvexPolyFilled(poly.data(), (int)poly.size(), fillCol);
        }

        // Face centroid label
        float cx = 0.f, cy = 0.f;
        for (auto& p : poly) { cx += p.x; cy += p.y; }
        if (!poly.empty()) { cx /= poly.size(); cy /= poly.size(); }

        char flabel[64];
        snprintf(flabel, sizeof(flabel), "F%d\n%s", f.id, f.label.c_str());
        ImVec4 fc = ImGui::ColorConvertU32ToFloat4(faceColour(f.label));
        fc.w = 0.9f;
        dl->AddText(ImVec2{cx - 14.f, cy - 8.f},
                    ImGui::ColorConvertFloat4ToU32(fc), flabel);
    }

    // ── Draw half-edges as directed arrows ───────────────────────────────────
    // Draw only the "face side" half-edges (face >= 0) to avoid duplicate arrows.
    // The "exterior" twin (face == -1) is skipped; instead mark it separately.
    for (const auto& he : graph.halfEdges) {
        if (he.face < 0) continue;  // skip exterior/twin side

        const auto* v0   = graph.vertex(he.vertex);
        const auto* twin = graph.halfEdge(he.twin);
        const auto* v1   = twin ? graph.vertex(twin->vertex) : nullptr;
        if (!v0 || !v1) continue;

        ImVec2 p0 = worldToScreen(v0->pos.x, v0->pos.y);
        ImVec2 p1 = worldToScreen(v1->pos.x, v1->pos.y);

        // Direction vector
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len < 1.f) continue;
        float nx = dx / len, ny = dy / len;
        float px = -ny, py =  nx;  // perpendicular (left of direction)

        // Shrink endpoints to not overlap vertex circles
        ImVec2 from = ImVec2{p0.x + nx * (kVtxR + 2.f),
                              p0.y + ny * (kVtxR + 2.f)};
        ImVec2 to   = ImVec2{p1.x - nx * (kVtxR + 4.f),
                              p1.y - ny * (kVtxR + 4.f)};

        // Offset outward slightly so twin arrows don't overlap
        const float kTwinOff = 4.f;
        from = ImVec2{from.x + px * kTwinOff, from.y + py * kTwinOff};
        to   = ImVec2{to.x   + px * kTwinOff, to.y   + py * kTwinOff};

        // Colour by r-label
        ImU32 col = halfEdgeColour(he.label);
        float thickness = (he.label.r == "open") ? 2.2f : 1.4f;

        dl->AddLine(from, to, col, thickness);

        // Arrowhead
        float ax0 = to.x - nx * kArrowLen + py * kArrowLen * 0.5f;
        float ay0 = to.y - ny * kArrowLen - px * kArrowLen * 0.5f;  // wrong sign fix:
        // Actually compute cleanly:
        float cos_a = std::cos(kArrowAng), sin_a = std::sin(kArrowAng);
        // Rotate -nx,-ny by ±kArrowAng
        ImVec2 ah1 = ImVec2{
            to.x + (-nx * cos_a + ny * sin_a) * kArrowLen,
            to.y + (-ny * cos_a - nx * sin_a) * kArrowLen
        };
        ImVec2 ah2 = ImVec2{
            to.x + (-nx * cos_a - ny * sin_a) * kArrowLen,
            to.y + (-ny * cos_a + nx * sin_a) * kArrowLen
        };
        (void)ax0; (void)ay0;
        dl->AddLine(to, ah1, col, thickness);
        dl->AddLine(to, ah2, col, thickness);

        // Edge label: HE id + r-value + θ, placed offset from midpoint
        ImVec2 mid = ImVec2{(from.x + to.x) * 0.5f + px * kLabelOff,
                             (from.y + to.y) * 0.5f + py * kLabelOff};
        char elabel[64];
        const char* rShort = he.label.r == "open"     ? "O"
                           : he.label.r == "exterior"  ? "X"
                           : he.label.r == "glued"     ? "G"
                           : he.label.r.c_str();
        snprintf(elabel, sizeof(elabel), "HE%d [%s] %.0f\xc2\xb0",
                 he.id, rShort,
                 he.label.theta * 180.f / 3.14159f);
        // Small white outline for readability
        dl->AddText(ImVec2{mid.x + 1.f, mid.y + 1.f},
                    IM_COL32(0, 0, 0, 180), elabel);
        dl->AddText(mid, col, elabel);
    }

    // ── Draw vertices as filled circles ──────────────────────────────────────
    for (const auto& v : graph.vertices) {
        ImVec2 sp = worldToScreen(v.pos.x, v.pos.y);

        dl->AddCircleFilled(sp, kVtxR,     IM_COL32(30,  35,  50,  255));
        dl->AddCircle      (sp, kVtxR + 1, IM_COL32(180, 200, 240, 200), 16, 1.5f);

        // Vertex ID label centred in circle
        char vlabel[16];
        snprintf(vlabel, sizeof(vlabel), "%d", v.id);
        ImVec2 tsz = ImGui::CalcTextSize(vlabel);
        dl->AddText(ImVec2{sp.x - tsz.x * 0.5f, sp.y - tsz.y * 0.5f},
                    IM_COL32(220, 230, 255, 255), vlabel);
    }

    return 0;  // no imnodes node IDs used
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

// drawDPORule is no longer used — L/I/R rendering is done inline
// in drawRulesTab() via three drawGraph() calls side by side.


// imnodes.cpp — vendored minimal implementation for Mythos.
// See imnodes.h for description. MIT licence.
//
// Implementation strategy:
//   - Each node is an ImGui child window rendered inside the editor canvas.
//   - The editor canvas is an ImGui child window with scrolling (pan) and
//     mouse-wheel zoom.
//   - Title bar is a coloured rectangle behind the first row of content.
//   - Pins are drawn as small circles at the left/right edge of the node.
//   - Links are bezier curves drawn on the editor's draw list.
//   - MiniMap is a small scaled-down copy drawn in a corner.
//   - No interaction beyond pan and zoom is implemented (read-only display).

#include "imnodes.h"
#include <imgui_internal.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cmath>

// ============================================================
// Internal state types
// ============================================================

struct ImNodesNodeState {
    int     id       = -1;
    ImVec2  pos      = {0.f, 0.f};   // canvas-space position
    ImVec2  size     = {0.f, 0.f};   // measured last frame
    bool    posSet   = false;         // was SetNodeEditorSpacePos called?
};

struct ImNodesColorOverride {
    ImNodesCol col;
    ImU32      value;
};

struct ImNodesLink {
    int id, startAttr, endAttr;
};

struct ImNodesAttributeState {
    int    id      = -1;
    ImVec2 pinPos  = {0.f, 0.f};  // screen pos of the pin circle
    bool   isInput = false;
};

// ============================================================
// Context
// ============================================================

struct ImNodesContext {
    // Canvas pan and zoom
    ImVec2 scrolling = {0.f, 0.f};
    float  zoom      = 1.f;

    // Per-node layout state (persists across frames)
    std::unordered_map<int, ImNodesNodeState> nodes;

    // Within-frame stacks
    std::vector<ImNodesColorOverride> colorStack;

    // Current frame data (reset at BeginNodeEditor)
    std::vector<ImNodesLink> links;

    // Active node being drawn
    int     activeNodeId       = -1;
    ImVec2  activeNodeStartCur = {0.f, 0.f};  // ImGui cursor before BeginNode
    bool    inTitleBar         = false;
    float   titleBarEndY       = 0.f;

    // Active attribute being drawn
    int  activeAttrId   = -1;
    bool activeAttrIsIn = false;

    // Canvas-space -> screen-space transform  (set in BeginNodeEditor)
    ImVec2 canvasOrigin = {0.f, 0.f}; // screen pos of canvas top-left

    // Pending node positions set before BeginNode (SetNodeEditorSpacePos)
    std::unordered_map<int, ImVec2> pendingPositions;

    // For minimap rendering
    ImVec2 editorSize = {0.f, 0.f};
};

// ============================================================
// Global context management
// ============================================================

static ImNodesContext* gCtx = nullptr;

namespace ImNodes {

ImNodesContext* CreateContext()
{
    ImNodesContext* ctx = new ImNodesContext();
    if (!gCtx) gCtx = ctx;
    return ctx;
}

void DestroyContext(ImNodesContext* ctx)
{
    if (!ctx) ctx = gCtx;
    if (gCtx == ctx) gCtx = nullptr;
    delete ctx;
}

void SetCurrentContext(ImNodesContext* ctx)
{
    gCtx = ctx;
}

// ============================================================
// Helpers
// ============================================================

// Convert canvas-space position to screen space.
static ImVec2 CanvasToScreen(const ImVec2& canvasPos)
{
    if (!gCtx) return canvasPos;
    return ImVec2{
        gCtx->canvasOrigin.x + (canvasPos.x + gCtx->scrolling.x) * gCtx->zoom,
        gCtx->canvasOrigin.y + (canvasPos.y + gCtx->scrolling.y) * gCtx->zoom
    };
}

static ImU32 GetColorOverride(ImNodesCol col, ImU32 def)
{
    if (!gCtx) return def;
    // Walk stack in reverse — last push wins
    for (int i = (int)gCtx->colorStack.size() - 1; i >= 0; --i)
        if (gCtx->colorStack[i].col == col)
            return gCtx->colorStack[i].value;
    return def;
}

// Default colours
static ImU32 DefaultNodeBg()   { return IM_COL32(50, 55, 70, 240); }
static ImU32 DefaultTitleBar() { return IM_COL32(40, 80, 140, 255); }
static ImU32 DefaultOutline()  { return IM_COL32(100, 110, 130, 200); }
static ImU32 DefaultGrid()     { return IM_COL32(30, 32, 40, 255); }
static ImU32 DefaultGridLine() { return IM_COL32(50, 54, 66, 80); }

// ============================================================
// BeginNodeEditor / EndNodeEditor
// ============================================================

void BeginNodeEditor()
{
    if (!gCtx) return;
    gCtx->links.clear();
    gCtx->activeNodeId = -1;

    // The editor canvas — fills the available content region
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50.f) canvasSize.x = 50.f;
    if (canvasSize.y < 50.f) canvasSize.y = 50.f;
    gCtx->editorSize = canvasSize;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    gCtx->canvasOrigin = p0;

    // Draw background
    dl->AddRectFilled(p0, ImVec2{p0.x + canvasSize.x, p0.y + canvasSize.y},
                      GetColorOverride(ImNodesCol_GridBackground, DefaultGrid()));

    // Draw grid lines
    float gridStep = 32.f * gCtx->zoom;
    ImU32 gridCol  = GetColorOverride(ImNodesCol_GridLine, DefaultGridLine());
    float offX = fmodf(gCtx->scrolling.x * gCtx->zoom, gridStep);
    float offY = fmodf(gCtx->scrolling.y * gCtx->zoom, gridStep);
    for (float x = offX; x < canvasSize.x; x += gridStep)
        dl->AddLine(ImVec2{p0.x + x, p0.y},
                    ImVec2{p0.x + x, p0.y + canvasSize.y}, gridCol);
    for (float y = offY; y < canvasSize.y; y += gridStep)
        dl->AddLine(ImVec2{p0.x, p0.y + y},
                    ImVec2{p0.x + canvasSize.x, p0.y + y}, gridCol);

    // Invisible widget to capture input for pan/zoom
    ImGui::InvisibleButton("##node_editor_canvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvasHovered = ImGui::IsItemHovered();

    // Pan (left drag or right drag)
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        gCtx->scrolling.x += delta.x / gCtx->zoom;
        gCtx->scrolling.y += delta.y / gCtx->zoom;
    }
    // Also allow middle-mouse pan
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        gCtx->scrolling.x += delta.x / gCtx->zoom;
        gCtx->scrolling.y += delta.y / gCtx->zoom;
    }

    // Zoom (mouse wheel)
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.f) {
        float newZoom = gCtx->zoom * (1.f + ImGui::GetIO().MouseWheel * 0.08f);
        newZoom = ImClamp(newZoom, 0.2f, 3.f);
        // Zoom toward cursor
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float relX = (mousePos.x - p0.x) / gCtx->zoom;
        float relY = (mousePos.y - p0.y) / gCtx->zoom;
        gCtx->scrolling.x -= relX * (newZoom - gCtx->zoom) / newZoom;
        gCtx->scrolling.y -= relY * (newZoom - gCtx->zoom) / newZoom;
        gCtx->zoom = newZoom;
    }

    // Set draw cursor back to p0 so nodes render inside the canvas area
    ImGui::SetCursorScreenPos(p0);
}

void EndNodeEditor()
{
    if (!gCtx) return;

    // Draw links
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& link : gCtx->links) {
        // TODO: draw bezier between pin positions when pin tracking is added
        (void)link;
    }
}

// ============================================================
// Node positioning
// ============================================================

void SetNodeEditorSpacePos(int node_id, const ImVec2& pos)
{
    if (!gCtx) return;
    gCtx->pendingPositions[node_id] = pos;
    gCtx->nodes[node_id].pos    = pos;
    gCtx->nodes[node_id].posSet = true;
}

void SetNodeGridSpacePos(int node_id, const ImVec2& pos)
{
    SetNodeEditorSpacePos(node_id, pos);
}

// ============================================================
// BeginNode / EndNode
// ============================================================

void BeginNode(int id)
{
    if (!gCtx) return;
    gCtx->activeNodeId = id;

    // Get or create state
    ImNodesNodeState& ns = gCtx->nodes[id];
    ns.id = id;

    // Screen position of this node
    ImVec2 screenPos = CanvasToScreen(ns.pos);

    // Push clip rect for node
    ImGui::SetCursorScreenPos(screenPos);

    // Start a group to measure the node's size
    ImGui::BeginGroup();
    gCtx->activeNodeStartCur = screenPos;
    gCtx->inTitleBar = false;
    gCtx->titleBarEndY = screenPos.y;

    // Push window-level item width
    ImGui::PushItemWidth(180.f * gCtx->zoom);
}

void EndNode()
{
    if (!gCtx) return;

    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // Measure node size
    ImVec2 nodeMin = ImGui::GetItemRectMin();
    ImVec2 nodeMax = ImGui::GetItemRectMax();

    // Padding
    float padX = 8.f * gCtx->zoom;
    float padY = 4.f * gCtx->zoom;
    nodeMin.x -= padX; nodeMin.y -= padY;
    nodeMax.x += padX; nodeMax.y += padY;

    ImNodesNodeState& ns = gCtx->nodes[gCtx->activeNodeId];
    ns.size = ImVec2{nodeMax.x - nodeMin.x, nodeMax.y - nodeMin.y};

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Node background
    ImU32 bgCol = GetColorOverride(ImNodesCol_NodeBackground, DefaultNodeBg());
    dl->AddRectFilled(nodeMin, nodeMax, bgCol, 6.f * gCtx->zoom);

    // Title bar fill (behind content)
    if (gCtx->titleBarEndY > nodeMin.y) {
        ImU32 tbCol = GetColorOverride(ImNodesCol_TitleBar, DefaultTitleBar());
        ImVec2 tbMax = { nodeMax.x, gCtx->titleBarEndY + padY };
        dl->AddRectFilled(nodeMin, tbMax, tbCol, 6.f * gCtx->zoom);
        // Square bottom corners of title bar
        dl->AddRectFilled(
            ImVec2{nodeMin.x, tbMax.y - 6.f * gCtx->zoom},
            tbMax, tbCol, 0.f);
    }

    // Node outline
    ImU32 outlineCol = GetColorOverride(ImNodesCol_NodeOutline, DefaultOutline());
    dl->AddRect(nodeMin, nodeMax, outlineCol, 6.f * gCtx->zoom, 0, 1.5f);

    // Re-draw content on top of background
    // (ImGui has already drawn the text — we just need the background behind it)
    // Use ImDrawList channel splitting for proper layering in future; for now
    // accept that text appears on top since ImGui draws groups front-to-back.

    gCtx->activeNodeId = -1;
}

// ============================================================
// Title bar
// ============================================================

void BeginNodeTitleBar()
{
    if (!gCtx) return;
    gCtx->inTitleBar = true;
    // Content will be drawn; we record the Y position after it ends
}

void EndNodeTitleBar()
{
    if (!gCtx) return;
    gCtx->inTitleBar  = false;
    gCtx->titleBarEndY = ImGui::GetCursorScreenPos().y;
    ImGui::Separator();
}

// ============================================================
// Attributes (pins)
// ============================================================

void BeginOutputAttribute(int id, ImNodesPinShape /*shape*/)
{
    if (!gCtx) return;
    gCtx->activeAttrId   = id;
    gCtx->activeAttrIsIn = false;
    ImGui::BeginGroup();
}

void EndOutputAttribute()
{
    if (!gCtx) return;
    ImGui::EndGroup();
    // Draw a small pin circle on the right edge of the group
    ImVec2 rMin = ImGui::GetItemRectMin();
    ImVec2 rMax = ImGui::GetItemRectMax();
    float cy    = (rMin.y + rMax.y) * 0.5f;
    float r     = 4.f * gCtx->zoom;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2{rMax.x + r, cy}, r, IM_COL32(180, 200, 220, 200));
    gCtx->activeAttrId = -1;
}

void BeginInputAttribute(int id, ImNodesPinShape /*shape*/)
{
    if (!gCtx) return;
    gCtx->activeAttrId   = id;
    gCtx->activeAttrIsIn = true;
    ImGui::BeginGroup();
}

void EndInputAttribute()
{
    if (!gCtx) return;
    ImGui::EndGroup();
    ImVec2 rMin = ImGui::GetItemRectMin();
    ImVec2 rMax = ImGui::GetItemRectMax();
    float cy    = (rMin.y + rMax.y) * 0.5f;
    float r     = 4.f * gCtx->zoom;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2{rMin.x - r, cy}, r, IM_COL32(180, 200, 220, 200));
    gCtx->activeAttrId = -1;
}

void BeginStaticAttribute(int /*id*/) { ImGui::BeginGroup(); }
void EndStaticAttribute()             { ImGui::EndGroup(); }

// ============================================================
// Links
// ============================================================

void Link(int id, int startAttr, int endAttr)
{
    if (!gCtx) return;
    gCtx->links.push_back({id, startAttr, endAttr});
}

// ============================================================
// Style
// ============================================================

void PushColorStyle(ImNodesCol item, ImU32 color)
{
    if (!gCtx) return;
    gCtx->colorStack.push_back({item, color});
}

void PopColorStyle()
{
    if (!gCtx || gCtx->colorStack.empty()) return;
    gCtx->colorStack.pop_back();
}

// ============================================================
// MiniMap
// ============================================================

void MiniMap(float fraction, ImNodesMiniMapLocation location,
             void (*)(int, void*), void*)
{
    if (!gCtx) return;
    if (gCtx->nodes.empty()) return;

    // Compute bounding box of all nodes in canvas space
    float minX =  1e9f, minY =  1e9f;
    float maxX = -1e9f, maxY = -1e9f;
    for (const auto& [id, ns] : gCtx->nodes) {
        minX = std::min(minX, ns.pos.x);
        minY = std::min(minY, ns.pos.y);
        maxX = std::max(maxX, ns.pos.x + 200.f); // estimate node width
        maxY = std::max(maxY, ns.pos.y + 80.f);
    }
    if (minX >= maxX || minY >= maxY) return;

    // MiniMap size
    float mmW = gCtx->editorSize.x * fraction;
    float mmH = gCtx->editorSize.y * fraction;
    float pad = 8.f;

    ImVec2 canvasOrigin = gCtx->canvasOrigin;
    ImVec2 mmPos;
    switch (location) {
        case ImNodesMiniMapLocation_BottomRight:
            mmPos = { canvasOrigin.x + gCtx->editorSize.x - mmW - pad,
                      canvasOrigin.y + gCtx->editorSize.y - mmH - pad };
            break;
        case ImNodesMiniMapLocation_BottomLeft:
            mmPos = { canvasOrigin.x + pad,
                      canvasOrigin.y + gCtx->editorSize.y - mmH - pad };
            break;
        case ImNodesMiniMapLocation_TopRight:
            mmPos = { canvasOrigin.x + gCtx->editorSize.x - mmW - pad,
                      canvasOrigin.y + pad };
            break;
        default: // TopLeft
            mmPos = { canvasOrigin.x + pad, canvasOrigin.y + pad };
            break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mmMax = { mmPos.x + mmW, mmPos.y + mmH };

    // Background
    dl->AddRectFilled(mmPos, mmMax, IM_COL32(20, 22, 30, 200), 4.f);
    dl->AddRect      (mmPos, mmMax, IM_COL32(80, 85, 100, 180), 4.f);

    // Scale: canvas bbox → minimap rect
    float scaleX = mmW / (maxX - minX);
    float scaleY = mmH / (maxY - minY);
    float scale  = std::min(scaleX, scaleY) * 0.85f;

    float offsetX = mmPos.x + (mmW - (maxX - minX) * scale) * 0.5f;
    float offsetY = mmPos.y + (mmH - (maxY - minY) * scale) * 0.5f;

    auto toMM = [&](ImVec2 p) -> ImVec2 {
        return { offsetX + (p.x - minX) * scale,
                 offsetY + (p.y - minY) * scale };
    };

    // Draw node rects in minimap
    for (const auto& [id, ns] : gCtx->nodes) {
        ImVec2 nMin = toMM(ns.pos);
        ImVec2 nMax = toMM({ ns.pos.x + (ns.size.x > 0 ? ns.size.x / gCtx->zoom : 150.f),
                              ns.pos.y + (ns.size.y > 0 ? ns.size.y / gCtx->zoom : 60.f) });
        dl->AddRectFilled(nMin, nMax, IM_COL32(60, 100, 160, 200), 2.f);
    }

    // Viewport indicator (what's currently visible in the editor)
    float vpW = gCtx->editorSize.x / gCtx->zoom;
    float vpH = gCtx->editorSize.y / gCtx->zoom;
    float vpX = -gCtx->scrolling.x;
    float vpY = -gCtx->scrolling.y;
    ImVec2 vpMin = toMM({ vpX, vpY });
    ImVec2 vpMax = toMM({ vpX + vpW, vpY + vpH });
    dl->AddRect(vpMin, vpMax, IM_COL32(200, 210, 240, 180), 0.f, 0, 1.5f);
}

} // namespace ImNodes

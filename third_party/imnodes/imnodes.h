// imnodes.h — vendored minimal implementation for Mythos.
//
// Provides the imnodes API subset used by GraphViewer.cpp.
// Full upstream: https://github.com/Nelarius/imnodes (MIT)
//
// This file is a clean-room minimal implementation of the imnodes API.
// It is NOT the upstream source — it provides the same external interface
// but with a simpler internal implementation sufficient for read-only
// graph display (no link dragging, no user interaction beyond pan/zoom).
//
// API covered:
//   Context lifecycle: CreateContext, DestroyContext, SetCurrentContext
//   Node lifecycle:    BeginNodeEditor, EndNodeEditor
//   Node drawing:      BeginNode, EndNode, BeginNodeTitleBar, EndNodeTitleBar
//   Attributes:        BeginOutputAttribute, EndOutputAttribute
//   Positioning:       SetNodeEditorSpacePos, SetNodeGridSpacePos (alias)
//   Styling:           PushColorStyle, PopColorStyle
//   MiniMap:           MiniMap(fraction, location)

#pragma once
#include <imgui.h>

// ---- Enums ----------------------------------------------------------------

typedef int ImNodesCol;
typedef int ImNodesMiniMapLocation;
typedef int ImNodesPinShape;

enum ImNodesCol_ {
    ImNodesCol_NodeBackground = 0,
    ImNodesCol_NodeBackgroundHovered,
    ImNodesCol_NodeBackgroundSelected,
    ImNodesCol_NodeOutline,
    ImNodesCol_TitleBar,
    ImNodesCol_TitleBarHovered,
    ImNodesCol_TitleBarSelected,
    ImNodesCol_Link,
    ImNodesCol_LinkHovered,
    ImNodesCol_LinkSelected,
    ImNodesCol_Pin,
    ImNodesCol_PinHovered,
    ImNodesCol_BoxSelector,
    ImNodesCol_BoxSelectorOutline,
    ImNodesCol_GridBackground,
    ImNodesCol_GridLine,
    ImNodesCol_GridLinePrimary,
    ImNodesCol_COUNT,
};

enum ImNodesMiniMapLocation_ {
    ImNodesMiniMapLocation_BottomLeft  = 0,
    ImNodesMiniMapLocation_BottomRight = 1,
    ImNodesMiniMapLocation_TopLeft     = 2,
    ImNodesMiniMapLocation_TopRight    = 3,
};

// ---- Context --------------------------------------------------------------

struct ImNodesContext;

// ---- Namespace ------------------------------------------------------------

namespace ImNodes {

IMGUI_API ImNodesContext* CreateContext();
IMGUI_API void            DestroyContext(ImNodesContext* ctx = nullptr);
IMGUI_API void            SetCurrentContext(ImNodesContext* ctx);

IMGUI_API void BeginNodeEditor();
IMGUI_API void EndNodeEditor();

// Position nodes in the editor canvas space (before BeginNode).
IMGUI_API void SetNodeEditorSpacePos(int node_id, const ImVec2& pos);
IMGUI_API void SetNodeGridSpacePos  (int node_id, const ImVec2& pos); // alias

IMGUI_API void BeginNode(int id);
IMGUI_API void EndNode();

IMGUI_API void BeginNodeTitleBar();
IMGUI_API void EndNodeTitleBar();

IMGUI_API void BeginOutputAttribute(int id, ImNodesPinShape shape = 1);
IMGUI_API void EndOutputAttribute();

IMGUI_API void BeginInputAttribute(int id, ImNodesPinShape shape = 1);
IMGUI_API void EndInputAttribute();

IMGUI_API void BeginStaticAttribute(int id);
IMGUI_API void EndStaticAttribute();

// Link two attribute pins.
IMGUI_API void Link(int id, int start_attr, int end_attr);

// Style
IMGUI_API void PushColorStyle(ImNodesCol item, ImU32 color);
IMGUI_API void PopColorStyle();

// MiniMap — renders a small overview in one corner of the editor.
IMGUI_API void MiniMap(float fraction = 0.2f,
                       ImNodesMiniMapLocation location = ImNodesMiniMapLocation_BottomLeft,
                       void (*node_hovering_cb)(int,void*) = nullptr,
                       void* node_hovering_cb_data = nullptr);

} // namespace ImNodes

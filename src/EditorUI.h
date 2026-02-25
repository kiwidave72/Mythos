#pragma once
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

// All ImGui panels live here.
// App calls render() once per frame between ImGui::NewFrame() and ImGui::Render()

// ---- Editor Mode -----------------------------------------------------------
// The editor has three top-level modes. Each mode owns its toolbar section,
// panel visibility set, and keyboard shortcut context.
//
//   EDITOR        Default 3D scene editing.
//   PLAY          Preview / simulation mode. All panels hidden, press P/ESC to exit.
//   GRAPH_GRAMMAR Grammar building and editing mode (Merrell DPO system).
//                 Toolbar shows grammar-specific buttons. Grammar panels visible.
//
enum class EditorMode
{
    EDITOR,
    PLAY,
    GRAPH_GRAMMAR,
};

// ---- ToolbarSection --------------------------------------------------------
// External modules (grammar-ui, future generators, etc.) register a toolbar
// section by calling EditorUI::registerToolbarSection() at startup.
//
// Each section declares:
//   requiredMode  — the EditorMode in which this section is shown.
//                   EditorMode::EDITOR means always show (never in PLAY).
//   draw          — callback that renders the section's ImGui buttons.
//                   Called inside the toolbar window, preceded by a divider.
//
struct ToolbarSection
{
    EditorMode              requiredMode;  // mode that activates this section
    std::function<void()>   draw;          // ImGui button calls go here
};

struct EditorUIState
{
    // Active editor mode
    EditorMode mode = EditorMode::EDITOR;

    // Stats (written by App each frame)
    float fps         = 0.f;
    int   numObjects  = 0;
    int   numSelected = 0;

    // Test editbox content
    std::string editboxText;

    // Window visibility — only Scene panel shown by default
    bool showTestWindow    = false;
    bool showAssetLibrary  = false;
    bool showGrammarView   = false;

    // Set true by App while the user is panning / zooming / orbiting the scene.
    bool  sceneInteracting  = false;

    // Panels are fully hidden (not just dimmed) during scene interaction.
    // panelsHidden goes true immediately when interaction starts, and returns
    // false only after the user has stopped interacting for kUiHoldoff seconds
    // — prevents flickering on short scroll ticks.
    bool   panelsHidden  = false;
    double uiHoldoffEnd  = 0.0;  // glfwGetTime() value when panels should reappear
    static constexpr double kUiHoldoff = 0.12;  // seconds after last interaction

    // Gizmo operation mode (ImGuizmo::OPERATION values stored as int)
    // 0=none  7=translate  120=rotate  896=scale
    int   gizmoOp = 7; // default: translate

    // View mode toggle — solid vs wireframe
    bool  wireframeMode = false;

    // Layout constants — read by App to position viewport/gizmo correctly.
    // menuBarHeight is set after drawMainMenuBar each frame; others are fixed.
    float menuBarHeight   = 0.f;
    float toolbarHeight   = 40.f;
    float statusBarHeight = 22.f;
    float scenePanelWidth = 300.f;

    // Filled by toolbar/menu when user selects files — App drains this each frame
    std::vector<std::string> importedPaths;

    // Project actions — set by menu, drained by App each frame
    bool        newProject    = false;
    bool        saveProject   = false;
    bool        loadProject   = false;
    std::string projectPath;

    // Status message shown briefly after save/load
    std::string statusMsg;
    double      statusExpiry = 0.0;

    // ---- Scene panel data (written by App each frame, read by drawScenePanel) ----

    // Outliner list — App fills this from scene.objects() each frame
    struct OutlinerEntry {
        int         id;
        std::string label;
        bool        selected = false;
    };
    std::vector<OutlinerEntry> outlinerEntries;

    // Set by drawScenePanel when user clicks an outliner row — App drains each frame
    int  outlinerClickId    = -1;
    bool outlinerClickShift = false;
    bool outlinerClickCtrl  = false;

    // Inspector — App writes primary-selection transform here each frame
    bool      inspectorVisible = false;
    glm::vec3 inspPos   = {0,0,0};
    glm::vec3 inspRot   = {0,0,0};
    glm::vec3 inspScale = {1,1,1};
    char      inspMeshInfo[128] = {};

    // Set by drawScenePanel when user edits a drag field — App reads and applies
    bool inspectorDirty  = false;   // value changed this frame (live preview)
    bool inspectorCommit = false;   // drag ended — push undo record
};

class EditorUI
{
public:
    void init();
    bool render(EditorUIState& state);

    // Register a toolbar section from an external module (e.g. grammar-ui).
    // Call once at startup, before the first render().
    // Sections are drawn in registration order after the built-in editor section.
    void registerToolbarSection(ToolbarSection section);

private:
    std::vector<ToolbarSection> m_toolbarSections;

    void drawMainMenuBar(EditorUIState& state);
    void drawToolbar(EditorUIState& state);
    void drawStatusBar(EditorUIState& state);   // Refactor F — always-docked bottom
    void drawScenePanel(EditorUIState& state);  // Refactor G — always-docked left
    void drawStatusToast(EditorUIState& state);
};

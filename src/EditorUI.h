#pragma once
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

// ============================================================
// EditorMode
// ============================================================
// The editor has three top-level modes. Each mode owns its toolbar section,
// panel visibility set, and keyboard shortcut context.
//
//   EDITOR        Default 3D scene editing.
//   PLAY          Preview / simulation. All panels hidden; P or ESC to exit.
//   GRAPH_GRAMMAR Grammar building and editing (Merrell DPO system).
//
enum class EditorMode
{
    EDITOR,
    PLAY,
    GRAPH_GRAMMAR,
};

// ============================================================
// ToolbarSection
// ============================================================
// External modules register toolbar sections at startup via
// EditorUI::registerToolbarSection().
//
// visibleModes: bitmask of EditorMode values. Use the helpers:
//   toolbarInMode(EditorMode m)          — show only in that mode
//   toolbarInModes(EditorMode a, EditorMode b) — show in both modes
//   toolbarExceptPlay()                  — show in EDITOR + GRAMMAR
//
// draw: called inside the toolbar window when the section is visible.
//       A separator is automatically prepended.
//
struct ToolbarSection
{
    // ---- Legacy single-mode constructor (Refactor C compatibility) ----------
    // Keeping this so existing GrammarView registration still compiles.
    EditorMode            requiredMode  = EditorMode::EDITOR;

    // ---- New multi-mode visibility mask -----------------------------------
    // If visibleInEditor / visibleInGrammar are both false AND requiredMode
    // is set, the old single-mode logic is used (backwards compat).
    bool visibleInEditor  = false;
    bool visibleInGrammar = false;
    // PLAY is always hidden — sections never show in PLAY mode.

    std::function<void()> draw;

    // ---- Convenience constructors -----------------------------------------

    // Show only in one mode (original Refactor C behaviour).
    static ToolbarSection forMode(EditorMode m, std::function<void()> fn) {
        ToolbarSection s;
        s.requiredMode  = m;
        s.visibleInEditor  = (m == EditorMode::EDITOR);
        s.visibleInGrammar = (m == EditorMode::GRAPH_GRAMMAR);
        s.draw = std::move(fn);
        return s;
    }

    // Show in EDITOR mode only.
    static ToolbarSection editorOnly(std::function<void()> fn) {
        return forMode(EditorMode::EDITOR, std::move(fn));
    }

    // Show in GRAPH_GRAMMAR mode only.
    static ToolbarSection grammarOnly(std::function<void()> fn) {
        return forMode(EditorMode::GRAPH_GRAMMAR, std::move(fn));
    }

    // Show in both EDITOR and GRAPH_GRAMMAR (never in PLAY).
    static ToolbarSection alwaysVisible(std::function<void()> fn) {
        ToolbarSection s;
        s.requiredMode     = EditorMode::EDITOR; // default, not used when both set
        s.visibleInEditor  = true;
        s.visibleInGrammar = true;
        s.draw = std::move(fn);
        return s;
    }
};

// ============================================================
// EditorUIState
// ============================================================
struct EditorUIState
{
    // Active editor mode
    EditorMode mode     = EditorMode::EDITOR;
    EditorMode prevMode = EditorMode::EDITOR;

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
    bool showGraphViewer   = false;   // MG-2.5 imnodes graph viewer

    // Set true by App while the user is panning/zooming/orbiting the scene.
    bool sceneInteracting = false;

    // Panels are fully hidden during scene interaction. panelsHidden goes true
    // immediately when interaction starts, returns false only after kUiHoldoff
    // seconds of inactivity — prevents flickering on short scroll ticks.
    bool   panelsHidden = false;
    double uiHoldoffEnd = 0.0;
    static constexpr double kUiHoldoff = 0.12;

    // Gizmo operation mode (ImGuizmo::OPERATION values as int)
    // 0=none  7=translate  120=rotate  896=scale
    int gizmoOp = 7;

    // View mode
    bool wireframeMode = false;

    // Layout constants — read by App to position viewport / gizmo.
    float menuBarHeight   = 0.f;
    float toolbarHeight   = 40.f;
    float statusBarHeight = 22.f;
    float scenePanelWidth = 300.f;

    // Filled by toolbar / menu when user selects files; App drains each frame.
    std::vector<std::string> importedPaths;

    // Project actions — set by menu, drained by App each frame.
    bool        newProject  = false;
    bool        saveProject = false;
    bool        loadProject = false;
    std::string projectPath;

    // Status message shown briefly after save/load
    std::string statusMsg;
    double      statusExpiry = 0.0;

    // ---- Outliner (written by App each frame, read by drawScenePanel) ------
    struct OutlinerEntry {
        int         id;
        std::string label;
        bool        selected = false;
    };
    std::vector<OutlinerEntry> outlinerEntries;

    // Set by drawScenePanel when user clicks an entry; App drains each frame.
    int  outlinerClickId    = -1;
    bool outlinerClickShift = false;
    bool outlinerClickCtrl  = false;

    // Inspector (App writes primary-selection transform here each frame)
    bool      inspectorVisible = false;
    glm::vec3 inspPos    = {0,0,0};
    glm::vec3 inspRot    = {0,0,0};
    glm::vec3 inspScale  = {1,1,1};
    char      inspMeshInfo[128] = {};

    bool inspectorDirty  = false;  // value changed this frame (live preview)
    bool inspectorCommit = false;  // drag ended — push undo record
};

// ============================================================
// EditorUI
// ============================================================
class EditorUI
{
public:
    void init();
    bool render(EditorUIState& state);

    // Register a toolbar section from an external module.
    // Call once at startup. Sections drawn in registration order.
    void registerToolbarSection(ToolbarSection section);

private:
    std::vector<ToolbarSection> m_toolbarSections;

    // Returns true if the given section should be shown in the given mode.
    static bool sectionVisible(const ToolbarSection& s, EditorMode mode);

    void drawMainMenuBar(EditorUIState& state);
    void drawToolbar    (EditorUIState& state);
    void drawStatusBar  (EditorUIState& state);
    void drawScenePanel (EditorUIState& state);
    void drawStatusToast(EditorUIState& state);
};

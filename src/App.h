#pragma once
#include "Renderer.h"
#include "EditorUI.h"
#include "InputRouter.h"
#include "../lib/grammar-ui/GrammarView.h"
#include "../lib/grammar-ui/GraphViewer.h"
#include "../lib/grammar-core/MerrellGrammar.h"
#include "Scene.h"
#include "AssetLibraryView.h"
#include "CommandHistory.h"
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <map>

class App
{
public:
    bool init(int width, int height, const std::string& title);
    void run();
    void shutdown();

private:
    GLFWwindow*      m_window   = nullptr;
    Renderer         m_renderer;
    EditorUI         m_ui;
    EditorUIState    m_uiState;
    InputRouter      m_input;
    GrammarView      m_grammar;          // legacy tile-loop grammar (Grammar.h)
    merrell::MerrellGrammar m_merrell;   // Merrell 2023 DPO system (MG-1+)
    GraphViewer      m_graphViewer;      // MG-2.5 imnodes viewer panel
    Scene            m_scene;
    MeshLibrary      m_meshLib;
    AssetLibraryView m_assetLibrary;

    Camera m_camera;
    bool   m_lmbDown      = false;
    bool   m_rmbDown      = false;
    bool   m_scrollActive = false;
    double m_lastMX  = 0.0;
    double m_lastMY  = 0.0;

    glm::ivec2 m_cursorCell  = {0,0};
    bool       m_cursorValid = false;

    glm::vec3 m_rayOrig = {0,0,0};
    glm::vec3 m_rayDir  = {0,0,0};

    double m_prevTime  = 0.0;
    double m_fpsTime   = 0.0;
    int    m_fpsFrames = 0;

    // ---- Command history ----
    CommandHistory m_history;

    // ---- Gizmo multi-object tracking ----
    bool m_gizmoWasUsing = false;

    struct TransformSnap { glm::vec3 pos, rot, scl; };
    // Snapshots of ALL selected objects at gizmo drag start
    std::map<int, TransformSnap> m_gizmoPreSnaps;
    // Pivot object's transform matrix at drag start
    glm::mat4 m_gizmoPivotPre = glm::mat4(1.f);

    // ---- Clipboard ----
    std::vector<SceneObject> m_clipboard;

    // Helpers
    void commitMultiTransformCommand(
        const std::map<int, TransformSnap>& pre,
        const std::map<int, TransformSnap>& post);

    void copySelection();
    void pasteClipboard();
    void deleteSelection();
    void addMergedToLibrary(std::shared_ptr<MeshAsset> asset, const std::string& name);

    void update(double dt);
    void render();
    void drawSceneActions();  // action buttons inside the docked scene panel
    void drawGizmo();

    static void cbMouseButton(GLFWwindow*, int, int, int);
    static void cbCursorPos  (GLFWwindow*, double, double);
    static void cbScroll     (GLFWwindow*, double, double);
    static void cbKey        (GLFWwindow*, int, int, int, int);

    void onMouseButton(int btn, int action, int mods);
    void onCursorPos  (double x, double y);
    void onScroll     (double xoff, double yoff);
    void onKey        (int key, int scancode, int action, int mods);
};

#include "EditorUI.h"
#include "FileDialog.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>

void EditorUI::init()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 4.f;
    s.FrameRounding     = 3.f;
    s.GrabRounding      = 3.f;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;
    s.ItemSpacing       = {8.f, 5.f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = {0.13f, 0.14f, 0.17f, 1.f};
    c[ImGuiCol_MenuBarBg]      = {0.10f, 0.11f, 0.13f, 1.f};
    c[ImGuiCol_Header]         = {0.22f, 0.40f, 0.72f, 0.6f};
    c[ImGuiCol_HeaderHovered]  = {0.28f, 0.50f, 0.90f, 0.7f};
    c[ImGuiCol_Button]         = {0.20f, 0.38f, 0.68f, 0.8f};
    c[ImGuiCol_ButtonHovered]  = {0.28f, 0.50f, 0.90f, 1.f};
    c[ImGuiCol_FrameBg]        = {0.18f, 0.19f, 0.23f, 1.f};
    c[ImGuiCol_FrameBgHovered] = {0.24f, 0.26f, 0.32f, 1.f};
}

bool EditorUI::render(EditorUIState& state)
{
    bool wantQuit = false;

    if (state.mode == EditorMode::PLAY)
    {
        ImGui::SetNextWindowPos({10.f, 10.f});
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::SetNextWindowSize({220.f, 0.f});
        ImGui::Begin("##playtip", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs     |
            ImGuiWindowFlags_NoNav        |
            ImGuiWindowFlags_NoMove);
        ImGui::TextColored({0.4f,1.f,0.4f,1.f}, "PLAY MODE");
        ImGui::TextDisabled("Press P or ESC to return");
        ImGui::End();
        return true;
    }

    // ---- Panel visibility ------------------------------------------------
    // Hide all panels immediately when scene interaction begins.
    // Restore them only after kUiHoldoff seconds of inactivity, so a
    // brief scroll tick doesn't cause a visible flicker.
    double now = ImGui::GetTime();
    if (state.sceneInteracting)
        state.uiHoldoffEnd = now + state.kUiHoldoff;
    state.panelsHidden = (now < state.uiHoldoffEnd);

    drawMainMenuBar(state);
    state.menuBarHeight = ImGui::GetFrameHeight();
    drawToolbar(state);
    drawStatusBar(state);

    if (!state.panelsHidden)
    {
        drawScenePanel(state);
        drawStatusToast(state);
    }

    return !wantQuit;
}

// ============================================================
// Main menu bar
// ============================================================

void EditorUI::drawMainMenuBar(EditorUIState& state)
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Project", "Ctrl+N"))
            state.newProject = true;

        if (ImGui::MenuItem("Open Project...", "Ctrl+O")) {
            auto paths = FileDialog::openFiles(
                "Open Project",
                {{"Graph Editor Project", "*.gep"}, {"All Files", "*.*"}},
                "gep");
            if (!paths.empty()) {
                state.projectPath = paths[0];
                state.loadProject = true;
            }
        }

        if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
            if (state.projectPath.empty()) {
                auto p = FileDialog::saveFile(
                    "Save Project",
                    {{"Graph Editor Project", "*.gep"}},
                    "gep");
                if (!p.empty()) state.projectPath = p;
            }
            if (!state.projectPath.empty())
                state.saveProject = true;
        }

        if (ImGui::MenuItem("Save Project As...")) {
            auto p = FileDialog::saveFile(
                "Save Project As",
                {{"Graph Editor Project", "*.gep"}},
                "gep");
            if (!p.empty()) {
                state.projectPath = p;
                state.saveProject = true;
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Import Mesh...", "Ctrl+I")) {
            auto paths = FileDialog::openFiles(
                "Import Mesh",
                {{"All Meshes", "*.obj;*.gltf;*.glb"}, {"OBJ", "*.obj"}, {"GLTF", "*.gltf;*.glb"}},
                "obj");
            for (auto& p : paths)
                state.importedPaths.push_back(p);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4"))
        { /* handled by OS */ }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows"))
    {
        ImGui::MenuItem("Asset Library",  nullptr, &state.showAssetLibrary);
        ImGui::MenuItem("Grammar View",   nullptr, &state.showGrammarView);
        ImGui::MenuItem("Test Editbox",   nullptr, &state.showTestWindow);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem("About Graph Editor");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ============================================================
// Toolbar section registration
// ============================================================

void EditorUI::registerToolbarSection(ToolbarSection section)
{
    m_toolbarSections.push_back(std::move(section));
}

// ============================================================
// Toolbar
// ============================================================

void EditorUI::drawToolbar(EditorUIState& state)
{
    float menuH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({0.f, menuH});
    ImGui::SetNextWindowSize({ImGui::GetIO().DisplaySize.x, 40.f});
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##toolbar", nullptr,
        ImGuiWindowFlags_NoDecoration    |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- PLAY / STOP -------------------------------------------------------
    if (state.mode == EditorMode::PLAY)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.65f, 0.15f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.80f, 0.20f, 1.f});
        if (ImGui::Button("  STOP  "))
            state.mode = EditorMode::EDITOR;
        ImGui::PopStyleColor(2);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f, 0.55f, 0.18f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.70f, 0.22f, 1.f});
        if (ImGui::Button("  PLAY  "))
            state.mode = EditorMode::PLAY;
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ---- Mode label --------------------------------------------------------
    ImVec4 modeCol;
    const char* modeLabel;
    switch (state.mode) {
        case EditorMode::PLAY:
            modeCol   = {0.4f, 1.0f, 0.4f, 1.f};
            modeLabel = "PLAY MODE";
            break;
        case EditorMode::GRAPH_GRAMMAR:
            modeCol   = {0.6f, 0.8f, 1.0f, 1.f};
            modeLabel = "GRAPH GRAMMAR";
            break;
        default:
            modeCol   = {0.8f, 0.8f, 0.8f, 1.f};
            modeLabel = "EDITOR";
            break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, modeCol);
    ImGui::Text("%s", modeLabel);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ---- Grammar mode toggle -----------------------------------------------
    {
        bool grammarActive = (state.mode == EditorMode::GRAPH_GRAMMAR);
        if (grammarActive) ImGui::PushStyleColor(ImGuiCol_Button, {0.20f, 0.40f, 0.65f, 1.f});
        else               ImGui::PushStyleColor(ImGuiCol_Button, {0.28f, 0.32f, 0.42f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.55f, 0.90f, 1.f});
        if (ImGui::Button(" Grammar "))
            state.mode = grammarActive ? EditorMode::EDITOR : EditorMode::GRAPH_GRAMMAR;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle Graph Grammar mode  (G)");
        ImGui::SameLine();
    }

    // ---- Import — visible in EDITOR and GRAPH_GRAMMAR ----------------------
    if (state.mode != EditorMode::PLAY)
    {
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button,        {0.28f, 0.32f, 0.42f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.38f, 0.44f, 0.60f, 1.f});
        if (ImGui::Button("  Import Mesh...  ")) {
            auto paths = FileDialog::openFiles(
                "Import Mesh",
                {{"All Meshes", "*.obj;*.gltf;*.glb"}, {"OBJ", "*.obj"}, {"GLTF", "*.gltf;*.glb"}},
                "obj");
            for (auto& p : paths)
                state.importedPaths.push_back(p);
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Import OBJ or GLTF/GLB mesh files (Ctrl+I)");
        ImGui::SameLine();
    }

    // ---- Gizmo + Wireframe — EDITOR only -----------------------------------
    if (state.mode == EditorMode::EDITOR)
    {
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("Gizmo:");
        ImGui::SameLine();

        struct GizmoBtn { const char* label; int op; const char* tip; };
        static const GizmoBtn btns[] = {
            { " T ", 7,   "Translate  W" },
            { " R ", 120, "Rotate     E" },
            { " S ", 896, "Scale      R" },
        };
        for (auto& btn : btns) {
            bool active = (state.gizmoOp == btn.op);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, {0.22f, 0.50f, 0.85f, 1.f});
            else        ImGui::PushStyleColor(ImGuiCol_Button, {0.28f, 0.32f, 0.42f, 1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.38f, 0.55f, 0.95f, 1.f});
            if (ImGui::Button(btn.label)) state.gizmoOp = btn.op;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", btn.tip);
            ImGui::SameLine();
        }

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (state.wireframeMode)
            ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.35f, 0.10f, 1.f});
        else
            ImGui::PushStyleColor(ImGuiCol_Button, {0.28f, 0.32f, 0.42f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.75f, 0.50f, 0.15f, 1.f});
        if (ImGui::Button(state.wireframeMode ? " Solid " : " Wire  "))
            state.wireframeMode = !state.wireframeMode;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle Solid / Wireframe view  (Z)");
        ImGui::SameLine();
    }

    // ---- Registered external sections -------------------------------------
    for (auto& section : m_toolbarSections)
    {
        bool show = false;
        if (state.mode == EditorMode::PLAY)
            show = false;
        else if (section.requiredMode == EditorMode::EDITOR)
            show = (state.mode == EditorMode::EDITOR);
        else if (section.requiredMode == EditorMode::GRAPH_GRAMMAR)
            show = (state.mode == EditorMode::GRAPH_GRAMMAR);
        else
            show = true;

        if (show)
        {
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            section.draw();
            ImGui::SameLine();
        }
    }

    ImGui::End();
}

// ============================================================
// Status bar  (Refactor F) — always-docked bottom strip
// ============================================================

void EditorUI::drawStatusBar(EditorUIState& state)
{
    ImGuiIO& io = ImGui::GetIO();
    float sbH   = state.statusBarHeight;

    ImGui::SetNextWindowPos({0.f, io.DisplaySize.y - sbH});
    ImGui::SetNextWindowSize({io.DisplaySize.x, sbH});
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {6.f, 3.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,  {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.08f, 0.09f, 0.10f, 1.f});

    ImGui::Begin("##statusbar", nullptr,
        ImGuiWindowFlags_NoDecoration    |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Left side — project name
    {
        const char* projName = state.projectPath.empty()
            ? "Untitled"
            : state.projectPath.c_str();
        // Strip directory, show filename only
        const char* slash = std::max(
            strrchr(projName, '/'), strrchr(projName, '\\'));
        if (slash) projName = slash + 1;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.55f, 0.65f, 0.80f, 1.f});
        ImGui::Text("%s", projName);
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Object / selection count
    if (state.numSelected > 0)
        ImGui::Text("%d objects  /  %d selected", state.numObjects, state.numSelected);
    else
        ImGui::Text("%d objects", state.numObjects);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Mode
    ImVec4 modeCol;
    const char* modeStr;
    switch (state.mode) {
        case EditorMode::PLAY:
            modeCol = {0.4f, 1.0f, 0.4f, 1.f}; modeStr = "PLAY"; break;
        case EditorMode::GRAPH_GRAMMAR:
            modeCol = {0.6f, 0.8f, 1.0f, 1.f}; modeStr = "GRAMMAR"; break;
        default:
            modeCol = {0.6f, 0.6f, 0.6f, 1.f}; modeStr = "EDITOR"; break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, modeCol);
    ImGui::Text("%s", modeStr);
    ImGui::PopStyleColor();

    // Right side — FPS (right-aligned)
    char fpsBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f fps", state.fps);
    float fpsW = ImGui::CalcTextSize(fpsBuf).x + 12.f;
    ImGui::SameLine(io.DisplaySize.x - fpsW);
    ImGui::TextDisabled("%s", fpsBuf);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ============================================================
// Scene panel  (Refactor G) — always-docked left strip
// ============================================================

void EditorUI::drawScenePanel(EditorUIState& state)
{
    ImGuiIO& io = ImGui::GetIO();

    float topY  = state.menuBarHeight + state.toolbarHeight;
    float botY  = io.DisplaySize.y - state.statusBarHeight;
    float panH  = botY - topY;
    float panW  = state.scenePanelWidth;

    ImGui::SetNextWindowPos ({0.f,  topY});
    ImGui::SetNextWindowSize({panW, panH});
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.f, 8.f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.11f, 0.12f, 0.15f, 1.f});

    ImGui::Begin("##scenepanel", nullptr,
        ImGuiWindowFlags_NoDecoration    |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- Header ----
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.85f, 1.f});
    ImGui::Text("SCENE");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // ---- Outliner ----
    float outlinerH = std::min(panH * 0.35f, 200.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.09f, 0.10f, 0.12f, 1.f});
    if (ImGui::BeginChild("##outliner", {-1.f, outlinerH}, false))
    {
        for (auto& entry : state.outlinerEntries)
        {
            ImGui::PushID(entry.id);
            bool sel = entry.selected;
            ImGui::PushStyleColor(ImGuiCol_Header,
                sel ? ImVec4{0.22f,0.40f,0.72f,0.8f}
                    : ImVec4{0,0,0,0});
            if (ImGui::Selectable(entry.label.c_str(), sel,
                                  ImGuiSelectableFlags_SpanAllColumns))
            {
                state.outlinerClickId    = entry.id;
                state.outlinerClickShift = ImGui::GetIO().KeyShift;
                state.outlinerClickCtrl  = ImGui::GetIO().KeyCtrl;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Separator();

    // ---- Transform inspector ----
    if (state.inspectorVisible)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.7f, 0.75f, 0.85f, 1.f});
        ImGui::Text("TRANSFORM");
        ImGui::PopStyleColor();

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##pos", &state.inspPos.x, 0.05f))
            state.inspectorDirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) state.inspectorCommit = true;
        ImGui::TextDisabled("position");

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##rot", &state.inspRot.x, 0.5f))
            state.inspectorDirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) state.inspectorCommit = true;
        ImGui::TextDisabled("rotation");

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##scl", &state.inspScale.x, 0.01f, 0.001f, 100.f))
            state.inspectorDirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) state.inspectorCommit = true;
        ImGui::TextDisabled("scale");

        ImGui::Separator();
        if (state.inspMeshInfo[0])
            ImGui::TextDisabled("%s", state.inspMeshInfo);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ============================================================
// Status toast
// ============================================================

void EditorUI::drawStatusToast(EditorUIState& state)
{
    if (state.statusMsg.empty()) return;

    double now    = ImGui::GetTime();
    double remain = state.statusExpiry - now;
    if (remain <= 0.0) { state.statusMsg.clear(); return; }

    float alpha = (remain < 0.5) ? (float)(remain / 0.5) : 1.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y - 50.f},
                            ImGuiCond_Always, {0.5f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.75f * alpha);
    ImGui::SetNextWindowSize({0, 0});
    ImGui::Begin("##status_toast", nullptr,
        ImGuiWindowFlags_NoDecoration   |
        ImGuiWindowFlags_NoInputs       |
        ImGuiWindowFlags_NoNav          |
        ImGuiWindowFlags_NoMove         |
        ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::TextColored({0.4f, 1.f, 0.5f, 1.f}, "%s", state.statusMsg.c_str());
    ImGui::PopStyleVar();
    ImGui::End();
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "App.h"
#include "FileDialog.h"
#include "ProjectFile.h"
#include "MeshMerge.h"
#include "../lib/grammar-core/HalfEdgeMesh.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>

static std::string exeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    size_t last = path.find_last_of("/\\");
    return (last != std::string::npos) ? path.substr(0, last + 1) : "./";
#else
    return "./";
#endif
}

// ============================================================
// Init
// ============================================================

bool App::init(int width, int height, const std::string& title)
{
    if (!glfwInit()) { std::cerr << "[App] glfwInit failed\n"; return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(0);  // vsync off — driver throttling caused 16-30fps on some GPUs

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[App] GLAD init failed\n"; return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetMouseButtonCallback(m_window, cbMouseButton);
    glfwSetCursorPosCallback  (m_window, cbCursorPos);
    glfwSetScrollCallback     (m_window, cbScroll);
    glfwSetKeyCallback        (m_window, cbKey);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    m_ui.init();

    if (!m_renderer.init()) { std::cerr << "[App] Renderer init failed\n"; return false; }

    std::string libPath = exeDir() + "editor_assets.json";
    m_assetLibrary.init(libPath);

    m_grammar.init(m_scene, m_meshLib);

    // Register grammar-ui toolbar section.
    // Shown only when mode == GRAPH_GRAMMAR — draws Generate / Reset / Step buttons.
    m_ui.registerToolbarSection({
        EditorMode::GRAPH_GRAMMAR,
        [this]() { m_grammar.drawToolbar(m_scene, m_meshLib); }
    });

    m_camera.target = {0,0,0};
    m_camera.yaw    = -45.f;
    m_camera.pitch  =  30.f;
    m_camera.dist   =  50.f;

    m_prevTime = glfwGetTime();
    m_fpsTime  = m_prevTime;
    glfwGetCursorPos(m_window, &m_lastMX, &m_lastMY);
    return true;
}

// ============================================================
// Run
// ============================================================

void App::run()
{
    const double kTargetFrameTime = 1.0 / 120.0;  // soft cap: 120 fps

    while (!glfwWindowShouldClose(m_window))
    {
        double now = glfwGetTime();
        double dt  = std::min(now - m_prevTime, 0.1);
        m_prevTime = now;

        m_fpsFrames++;
        if (now - m_fpsTime >= 1.0) {
            m_uiState.fps = (float)(m_fpsFrames / (now - m_fpsTime));
            m_fpsFrames   = 0;
            m_fpsTime     = now;
        }

        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        m_input.update();
        update(dt);
        render();

        glfwSwapBuffers(m_window);

        // Soft frame cap — sleep off any spare time so we don't burn 100% CPU
        double elapsed = glfwGetTime() - now;
        if (elapsed < kTargetFrameTime) {
            double sleepSec = kTargetFrameTime - elapsed;
            // Use a slightly shorter sleep to avoid overshooting on wake
            if (sleepSec > 0.001)
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(sleepSec - 0.001));
        }
    }
}

// ============================================================
// Update
// ============================================================

void App::update(double dt)
{
    m_grammar.update(m_scene, m_meshLib, dt);
    m_uiState.numObjects  = m_scene.objectCount();
    m_uiState.numSelected = m_scene.selectedCount();

    // ---- Fill outliner entries ----
    m_uiState.outlinerEntries.clear();
    for (auto& obj : m_scene.objects()) {
        char label[128];
        snprintf(label, sizeof(label), "%s##%d", obj.name.c_str(), obj.id);
        m_uiState.outlinerEntries.push_back({
            obj.id, std::string(label), m_scene.isSelected(obj.id)
        });
    }

    // ---- Fill inspector from primary selection ----
    auto* sel = m_scene.findById(m_scene.selectedId());
    m_uiState.inspectorVisible = (sel != nullptr);
    if (sel) {
        if (!m_uiState.inspectorDirty) {
            // Only sync from scene when not mid-drag (prevents fighting)
            m_uiState.inspPos   = sel->position;
            m_uiState.inspRot   = sel->rotation;
            m_uiState.inspScale = sel->scale;
        }
        if (sel->mesh)
            snprintf(m_uiState.inspMeshInfo, sizeof(m_uiState.inspMeshInfo),
                     "%s  (%d tris)", sel->mesh->name.c_str(),
                     (int)sel->mesh->data.indices.size() / 3);
        else
            m_uiState.inspMeshInfo[0] = '\0';
    }

    m_uiState.sceneInteracting =
        (m_uiState.mode != EditorMode::PLAY) &&
        m_input.sceneOwnsMouse() &&
        (m_lmbDown || m_rmbDown || m_scrollActive);
    m_scrollActive = false;

    m_cursorValid = false;
    if (m_input.sceneOwnsMouse()) {
        int fw, fh;
        glfwGetFramebufferSize(m_window, &fw, &fh);
        auto [ro, rd] = m_camera.screenRay((float)m_lastMX, (float)m_lastMY, fw, fh);
        m_rayOrig = ro;
        m_rayDir  = rd;

        if (std::abs(rd.y) > 1e-4f) {
            float t = -ro.y / rd.y;
            if (t > 0.f) {
                glm::vec3 hit = ro + rd * t;
                m_cursorCell  = { (int)std::floor(hit.x + 0.5f),
                                  (int)std::floor(hit.z + 0.5f) };
                m_cursorValid = true;
            }
        }
    }

    m_scene.setCursorCell(m_cursorCell, m_cursorValid);
}

// ============================================================
// Render
// ============================================================

void App::render()
{
    int fw, fh;
    glfwGetFramebufferSize(m_window, &fw, &fh);

    m_renderer.beginFrame(fw, fh);

    // Wireframe mode — apply before drawing scene objects, restore after
    if (m_uiState.wireframeMode)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Draw opaque geometry first so the grid depth-tests against it correctly
    for (auto& obj : m_scene.objects())
        m_renderer.drawSceneObject(m_camera, obj, fw, fh);

    m_grammar.drawLivePath(m_renderer, m_camera, fw, fh);

    // Restore fill before grid and ghost cube (they must always render solid)
    if (m_uiState.wireframeMode)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Grid drawn after opaque geometry — uses depth test (GL_LESS) with no
    // depth writes so it composites correctly over the background.
    m_renderer.drawGrid(m_camera, fw, fh);

    if (m_cursorValid && (m_uiState.mode != EditorMode::PLAY) &&
        m_scene.objectAtCell(m_cursorCell) != -1)
    {
        glm::mat4 model = glm::mat4(1.f);
        model = glm::translate(model,
            glm::vec3((float)m_cursorCell.x, 0.15f, (float)m_cursorCell.y));
        model = glm::scale(model, glm::vec3(1.f, 0.3f, 1.f));
        m_renderer.drawGhostCube(m_camera, model, {0.9f,0.9f,1.f}, 0.85f, fw, fh);
    }

    m_renderer.endFrame();

    // ---- ImGui ----
    bool keepRunning = m_ui.render(m_uiState);
    if (!keepRunning) glfwSetWindowShouldClose(m_window, true);

    ImGuiIO& io = ImGui::GetIO();

    // File shortcuts
    if (ImGui::IsKeyPressed(ImGuiKey_I) && io.KeyCtrl) {
        auto paths = FileDialog::openFiles("Import Mesh",
            {{"All Meshes","*.obj;*.gltf;*.glb"},{"OBJ","*.obj"},{"GLTF","*.gltf;*.glb"}},"obj");
        for (auto& p : paths) m_uiState.importedPaths.push_back(p);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_N) && io.KeyCtrl)
        m_uiState.newProject = true;
    if (ImGui::IsKeyPressed(ImGuiKey_S) && io.KeyCtrl) {
        if (m_uiState.projectPath.empty()) {
            auto p = FileDialog::saveFile("Save Project",{{"Graph Editor Project","*.gep"}},"gep");
            if (!p.empty()) m_uiState.projectPath = p;
        }
        if (!m_uiState.projectPath.empty()) m_uiState.saveProject = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O) && io.KeyCtrl) {
        auto paths = FileDialog::openFiles("Open Project",
            {{"Graph Editor Project","*.gep"},{"All Files","*.*"}},"gep");
        if (!paths.empty()) { m_uiState.projectPath = paths[0]; m_uiState.loadProject = true; }
    }

    // ---- Project actions ----
    if (m_uiState.newProject) {
        m_uiState.newProject = false;
        m_scene.clear(); m_history.clear();
        m_uiState.projectPath.clear();
        m_camera.target = {0,0,0}; m_camera.yaw = -45.f;
        m_camera.pitch = 30.f; m_camera.dist = 50.f;
        m_uiState.statusMsg = "New project";
        m_uiState.statusExpiry = glfwGetTime() + 2.0;
    }
    if (m_uiState.saveProject && !m_uiState.projectPath.empty()) {
        m_uiState.saveProject = false;
        if (ProjectFile::save(m_uiState.projectPath, m_camera, m_grammar, m_scene))
            m_uiState.statusMsg = "Saved: " + m_uiState.projectPath;
        else
            m_uiState.statusMsg = "Save failed: " + ProjectFile::lastError();
        m_uiState.statusExpiry = glfwGetTime() + 3.0;
    }
    if (m_uiState.loadProject && !m_uiState.projectPath.empty()) {
        m_uiState.loadProject = false;
        if (ProjectFile::load(m_uiState.projectPath, m_camera, m_grammar, m_scene, m_meshLib)) {
            m_history.clear();
            m_uiState.statusMsg = "Loaded: " + m_uiState.projectPath;
        } else {
            m_uiState.statusMsg = "Load failed: " + ProjectFile::lastError();
        }
        m_uiState.statusExpiry = glfwGetTime() + 3.0;
    }

    if (m_uiState.mode != EditorMode::PLAY && !m_uiState.panelsHidden) {
        // Asset Library
        m_assetLibrary.setOpen(m_uiState.showAssetLibrary);
        m_assetLibrary.draw(m_scene, m_uiState.importedPaths);
        m_uiState.showAssetLibrary = m_assetLibrary.isOpen();

        // Grammar View — synced with menu toggle
        m_grammar.setOpen(m_uiState.showGrammarView);
        m_grammar.drawPanel(m_scene, m_meshLib);
        m_uiState.showGrammarView = m_grammar.isOpen();

        drawSceneActions();
    }

    // ---- Drain outliner click ----
    if (m_uiState.outlinerClickId >= 0) {
        int id = m_uiState.outlinerClickId;
        if (m_uiState.outlinerClickShift)
            m_scene.selectAdd(id);
        else
            m_scene.selectById(id);
        m_uiState.outlinerClickId = -1;
    }

    // ---- Drain inspector edits ----
    if (m_uiState.inspectorDirty) {
        auto* sel = m_scene.findById(m_scene.selectedId());
        if (sel) {
            sel->position = m_uiState.inspPos;
            sel->rotation = m_uiState.inspRot;
            sel->scale    = m_uiState.inspScale;
        }
        m_uiState.inspectorDirty = false;
    }
    if (m_uiState.inspectorCommit) {
        auto* sel = m_scene.findById(m_scene.selectedId());
        if (sel) {
            std::map<int, TransformSnap> pre, post;
            pre[sel->id]  = { sel->position, sel->rotation, sel->scale };
            post[sel->id] = { sel->position, sel->rotation, sel->scale };
            commitMultiTransformCommand(pre, post);
        }
        m_uiState.inspectorCommit = false;
    }

    if (m_uiState.mode != EditorMode::PLAY && !m_uiState.panelsHidden)
        drawGizmo();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ============================================================
// Scene panel
// ============================================================

// ============================================================
// Scene actions (buttons in the docked scene panel)
// Called from render() — draws into the always-docked ##scenepanel window
// that EditorUI created this frame. Uses BeginChild to scroll independently.
// ============================================================

void App::drawSceneActions()
{
    // We're inside the ##scenepanel window created by EditorUI::drawScenePanel.
    // Add the action buttons below the transform inspector.
    // Use a child region so the buttons scroll without affecting the inspector.

    auto* sel = m_scene.findById(m_scene.selectedId());
    if (!sel) return;

    // Small scrollable region for all the action buttons
    ImGui::BeginChild("##actions", {-1.f, -1.f}, false,
                      ImGuiWindowFlags_NoScrollbar);

    // ---- Snap / Fit ----
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.22f,0.45f,0.30f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f,0.60f,0.40f,1.f});
    if (ImGui::Button("Snap to Grid", {-1,0})) {
        std::map<int,TransformSnap> pre, post;
        pre[sel->id] = { sel->position, sel->rotation, sel->scale };
        sel->position.x = std::round(sel->position.x / kGridCell) * kGridCell;
        sel->position.z = std::round(sel->position.z / kGridCell) * kGridCell;
        sel->position.y = 0.f;
        post[sel->id] = { sel->position, sel->rotation, sel->scale };
        commitMultiTransformCommand(pre, post);
    }
    ImGui::PopStyleColor(2);

    ImGui::PushStyleColor(ImGuiCol_Button,        {0.22f,0.35f,0.55f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f,0.48f,0.75f,1.f});
    if (ImGui::Button("Fit to Grid Cell", {-1,0})) {
        if (sel->mesh) {
            std::map<int,TransformSnap> pre, post;
            pre[sel->id] = { sel->position, sel->rotation, sel->scale };
            const glm::vec3 sz = sel->mesh->data.size();
            float xzMax = std::max(sz.x, sz.z);
            if (xzMax > 1e-4f) sel->scale = glm::vec3(kGridCell / xzMax);
            post[sel->id] = { sel->position, sel->rotation, sel->scale };
            commitMultiTransformCommand(pre, post);
        }
    }
    ImGui::PopStyleColor(2);

    // ---- Copy / Paste ----
    ImGui::Separator();
    if (ImGui::Button("Copy  (Ctrl+C)", {-1,0})) copySelection();
    ImGui::BeginDisabled(m_clipboard.empty());
    if (ImGui::Button("Paste (Ctrl+V)", {-1,0})) pasteClipboard();
    ImGui::EndDisabled();

    // ---- Merge ----
    ImGui::Separator();
    ImGui::BeginDisabled(m_scene.selectedCount() < 2);

    ImGui::PushStyleColor(ImGuiCol_Button,        {0.45f,0.35f,0.15f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.65f,0.50f,0.20f,1.f});

    auto doMerge = [&](bool weld) {
        std::vector<const SceneObject*> objs;
        for (int id : m_scene.selectedIds())
            if (auto* o = m_scene.findById(id)) objs.push_back(o);
        if (objs.size() < 2) return;

        std::vector<SceneObject> snapshots;
        for (auto* o : objs) snapshots.push_back(*o);

        std::string mname = "merged_" + std::to_string(m_scene.selectedId());
        MeshMerge::Result res = weld
            ? MeshMerge::mergeAndWeld(objs, mname, 0.001f)
            : MeshMerge::merge(objs, mname);

        if (!res.asset->upload()) return;

        addMergedToLibrary(res.asset, mname);

        SceneObject& newObj = m_scene.addObject();
        newObj.name   = mname;
        newObj.primId = mname;
        newObj.mesh   = res.asset;
        newObj.color  = objs[0]->color;
        int newId = newObj.id;

        std::vector<int> oldIds;
        for (auto* o : objs) oldIds.push_back(o->id);
        for (int id : oldIds) m_scene.removeObject(id);
        m_scene.selectById(newId);

        std::string cmdName = std::string(weld ? "Merge+Weld " : "Merge ") +
                              std::to_string(snapshots.size()) + " objects";
        m_history.execute({
            cmdName,
            [](){},
            [this, snapshots, newId]() {
                m_scene.removeObject(newId);
                for (const auto& snap : snapshots) {
                    SceneObject& o = m_scene.addObject();
                    o = snap;
                }
                m_scene.selectNone();
            }
        });
    };

    if (ImGui::Button("Merge Selected", {-1,0})) doMerge(false);
    if (ImGui::IsItemHovered() && m_scene.selectedCount() >= 2)
        ImGui::SetTooltip("Merge meshes, preserve materials. Added to Asset Library.");

    if (ImGui::Button("Merge + Weld Vertices", {-1,0})) doMerge(true);
    if (ImGui::IsItemHovered() && m_scene.selectedCount() >= 2)
        ImGui::SetTooltip("Merge + weld shared vertices (watertight). Added to Asset Library.");

    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && m_scene.selectedCount() < 2)
        ImGui::SetTooltip("Select 2 or more objects to merge");

    // ---- Half-Edge Split ----
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f,0.40f,0.55f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f,0.55f,0.75f,1.f});

    bool hasMesh = sel->mesh && !sel->mesh->data.indices.empty();
    ImGui::BeginDisabled(!hasMesh);
    if (ImGui::Button("Build Half-Edge Split", {-1,0})) {
        grammar::HalfEdgeMesh hem;
        std::cout << "\n[HalfEdge] ======= Building from: "
                  << sel->name << " =======\n";
        bool ok = hem.buildFromMesh(sel->mesh->data, 0.0001f);
        if (ok) {
            hem.dumpStats();
            hem.dumpFaces(20);
            hem.dumpEdges(30);
            hem.dumpBoundaryLoops();
            hem.dumpNonManifold(10);
            std::vector<std::string> errors;
            bool valid = hem.validate(&errors);
            if (valid)
                std::cout << "[HalfEdge] Validation: PASSED\n\n";
            else {
                std::cout << "[HalfEdge] Validation: FAILED ("
                          << errors.size() << " errors)\n";
                for (auto& e : errors) std::cout << "  " << e << "\n";
                std::cout << "\n";
            }
        } else {
            std::cout << "[HalfEdge] Build FAILED\n\n";
        }
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    if (ImGui::IsItemHovered() && !hasMesh)
        ImGui::SetTooltip("Select an object with mesh data");
    if (ImGui::IsItemHovered() && hasMesh)
        ImGui::SetTooltip("Build half-edge structure from mesh.\nResults printed to console.");

    // ---- Delete ----
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.55f,0.15f,0.15f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.75f,0.20f,0.20f,1.f});
    if (ImGui::Button("Delete", {-1,0})) deleteSelection();
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
}

// ============================================================
// Shutdown
// ============================================================

void App::shutdown()
{
    m_assetLibrary.shutdown();
    m_renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

// ============================================================
// GLFW callbacks
// ============================================================

void App::cbMouseButton(GLFWwindow* w,int b,int a,int m)
{ static_cast<App*>(glfwGetWindowUserPointer(w))->onMouseButton(b,a,m); }
void App::cbCursorPos(GLFWwindow* w,double x,double y)
{ static_cast<App*>(glfwGetWindowUserPointer(w))->onCursorPos(x,y); }
void App::cbScroll(GLFWwindow* w,double x,double y)
{ static_cast<App*>(glfwGetWindowUserPointer(w))->onScroll(x,y); }
void App::cbKey(GLFWwindow* w,int k,int s,int a,int m)
{ static_cast<App*>(glfwGetWindowUserPointer(w))->onKey(k,s,a,m); }

void App::onMouseButton(int btn, int action, int mods)
{
    if (!m_input.sceneOwnsMouse()) return;

    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        m_lmbDown = (action == GLFW_PRESS);

        if (action == GLFW_PRESS && !ImGuizmo::IsOver()) {
            int hitId = m_scene.pickObject(m_rayOrig, m_rayDir);
            bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

            if (hitId != -1) {
                if (shiftHeld) {
                    if (m_scene.isSelected(hitId)) m_scene.deselectById(hitId);
                    else                           m_scene.selectAdd(hitId);
                } else {
                    m_scene.selectById(hitId);
                }
            } else {
                if (!shiftHeld) m_scene.selectNone();
            }
        }
    }
    if (btn == GLFW_MOUSE_BUTTON_RIGHT)
        m_rmbDown = (action == GLFW_PRESS);
}

void App::onCursorPos(double x, double y)
{
    float dx = (float)(x - m_lastMX);
    float dy = (float)(y - m_lastMY);
    m_lastMX = x; m_lastMY = y;

    if (!m_input.sceneOwnsMouse()) return;
    if (m_lmbDown && !ImGuizmo::IsUsing()) m_camera.orbit(dx * 0.5f, -dy * 0.5f);
    if (m_rmbDown) m_camera.pan(dx, dy);
}

void App::onScroll(double /*xoff*/, double yoff)
{
    if (!m_input.sceneOwnsMouse()) return;

    // Find the world-space point on the Y=0 plane under the mouse cursor.
    // We'll keep that point stationary as we zoom (like map zoom in Blender/UE5).
    int fw, fh;
    glfwGetFramebufferSize(m_window, &fw, &fh);
    auto [ro, rd] = m_camera.screenRay((float)m_lastMX, (float)m_lastMY, fw, fh);

    // Ray-plane intersection with Y=0
    glm::vec3 worldPoint = m_camera.target; // fallback if ray is parallel to plane
    if (std::abs(rd.y) > 1e-4f)
    {
        float t = -ro.y / rd.y;
        if (t > 0.f)
            worldPoint = ro + rd * t;
    }

    float oldDist = m_camera.dist;
    m_camera.zoom((float)yoff);
    float newDist = m_camera.dist;

    // Shift target so the world point under the cursor stays fixed.
    // As dist shrinks, we move the target toward worldPoint proportionally.
    float ratio = newDist / oldDist;   // <1 when zooming in, >1 when zooming out
    m_camera.target = glm::mix(worldPoint, m_camera.target, ratio);

    m_scrollActive = true;
}

void App::onKey(int key, int /*sc*/, int action, int mods)
{
    if (action == GLFW_RELEASE) return;

    bool ctrl  = (mods & GLFW_MOD_CONTROL) != 0;
    bool shift = (mods & GLFW_MOD_SHIFT)   != 0;

    // ESC: cancel/deselect — never quit
    if (key == GLFW_KEY_ESCAPE) {
        if (m_uiState.mode == EditorMode::PLAY) {
            m_uiState.mode = EditorMode::EDITOR;
        } else if (m_uiState.mode == EditorMode::GRAPH_GRAMMAR) {
            m_uiState.mode = EditorMode::EDITOR;
        } else if (m_scene.selectedCount() > 0) {
            m_scene.selectNone();  // deselect
        } else if (m_uiState.gizmoOp != 0) {
            m_uiState.gizmoOp = 0; // cancel gizmo mode
        }
        return;
    }

    // Undo / Redo — work regardless of keyboard focus
    if (ctrl && key == GLFW_KEY_Z) { m_history.undo(); return; }
    if (ctrl && key == GLFW_KEY_Y) { m_history.redo(); return; }
    if (ctrl && shift && key == GLFW_KEY_Z) { m_history.redo(); return; }

    // Copy / Paste / Select All
    if (ctrl && key == GLFW_KEY_C) { copySelection(); return; }
    if (ctrl && key == GLFW_KEY_V) { pasteClipboard(); return; }
    if (ctrl && key == GLFW_KEY_A) { m_scene.selectAll(); return; }

    if (!m_input.sceneOwnsKeyboard()) return;

    if (key == GLFW_KEY_P) {
        m_uiState.mode = (m_uiState.mode == EditorMode::PLAY)
            ? EditorMode::EDITOR : EditorMode::PLAY;
    }
    if (key == GLFW_KEY_G) {
        m_uiState.mode = (m_uiState.mode == EditorMode::GRAPH_GRAMMAR)
            ? EditorMode::EDITOR : EditorMode::GRAPH_GRAMMAR;
    }
    if (key == GLFW_KEY_Z) m_uiState.wireframeMode = !m_uiState.wireframeMode;
    if (key == GLFW_KEY_F) m_camera.target = {0,0,0};

    if (key == GLFW_KEY_W) m_uiState.gizmoOp = 7;
    if (key == GLFW_KEY_E) m_uiState.gizmoOp = 120;
    if (key == GLFW_KEY_R) m_uiState.gizmoOp = 896;
    if (key == GLFW_KEY_Q) m_uiState.gizmoOp = 0;

    if (key == GLFW_KEY_DELETE) deleteSelection();
}

// ============================================================
// Gizmo — applies delta to ALL selected objects
// ============================================================

void App::drawGizmo()
{
    SceneObject* pivot = m_scene.findById(m_scene.selectedId());
    if (!pivot) return;
    if (m_uiState.gizmoOp == 0) return;

    int fw, fh;
    glfwGetFramebufferSize(m_window, &fw, &fh);
    if (fw == 0 || fh == 0) return;

    float aspect = (float)fw / (float)fh;
    glm::mat4 view = m_camera.viewMatrix();
    glm::mat4 proj = m_camera.projMatrix(aspect);

    ImGuizmo::SetRect(0.f, 0.f, (float)fw, (float)fh);
    ImGuizmo::SetOrthographic(false);

    glm::mat4 pivotMatrix = pivot->transform();

    ImGuizmo::OPERATION op = static_cast<ImGuizmo::OPERATION>(m_uiState.gizmoOp);
    float rotSnap[3]  = {90.f, 90.f, 90.f};
    float* snapValues = (op == ImGuizmo::ROTATE) ? rotSnap : nullptr;

    bool isUsing = ImGuizmo::IsUsing();

    // Snapshot ALL selected at drag start
    if (isUsing && !m_gizmoWasUsing) {
        m_gizmoPreSnaps.clear();
        m_gizmoPivotPre = pivotMatrix;
        for (int id : m_scene.selectedIds()) {
            if (auto* o = m_scene.findById(id))
                m_gizmoPreSnaps[id] = { o->position, o->rotation, o->scale };
        }
    }

    glm::mat4 newPivotMatrix = pivotMatrix;
    ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(proj),
        op, ImGuizmo::LOCAL,
        glm::value_ptr(newPivotMatrix),
        nullptr, snapValues);

    if (ImGuizmo::IsUsing() && m_scene.selectedCount() > 0) {
        // Compute delta: newPivot * inverse(oldPivot)
        glm::mat4 delta = newPivotMatrix * glm::inverse(m_gizmoPivotPre);

        // Apply decomposed pivot changes
        glm::vec3 skew; glm::vec4 persp; glm::quat orient; glm::vec3 pos, scale;
        if (glm::decompose(newPivotMatrix, scale, orient, pos, skew, persp)) {
            pivot->position = pos;
            pivot->scale    = scale;
            pivot->rotation = glm::degrees(glm::eulerAngles(orient));
        }

        // Apply same delta to all other selected objects
        if (m_scene.selectedCount() > 1) {
            for (int id : m_scene.selectedIds()) {
                if (id == m_scene.selectedId()) continue;
                auto* o = m_scene.findById(id);
                if (!o) continue;

                // Transform the other object's world matrix by the delta
                glm::mat4 newM = delta * o->transform();
                glm::vec3 ps, ro, sc; glm::quat qt; glm::vec4 pp;
                if (glm::decompose(newM, sc, qt, ps, ro, pp)) {
                    o->position = ps;
                    o->scale    = sc;
                    o->rotation = glm::degrees(glm::eulerAngles(qt));
                }
            }
        }

        // Keep pivot snapshot current so delta is frame-relative
        m_gizmoPivotPre = newPivotMatrix;
    }

    // Commit undo when drag ends
    if (!isUsing && m_gizmoWasUsing && !m_gizmoPreSnaps.empty()) {
        std::map<int, TransformSnap> post;
        for (auto& [id, _] : m_gizmoPreSnaps) {
            if (auto* o = m_scene.findById(id))
                post[id] = { o->position, o->rotation, o->scale };
        }
        commitMultiTransformCommand(m_gizmoPreSnaps, post);
        m_gizmoPreSnaps.clear();
    }

    m_gizmoWasUsing = isUsing;

    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
        m_uiState.sceneInteracting = true;
}

// ============================================================
// Command helpers
// ============================================================

void App::commitMultiTransformCommand(
    const std::map<int, TransformSnap>& pre,
    const std::map<int, TransformSnap>& post)
{
    // Skip if nothing actually changed
    bool changed = false;
    for (auto& [id, ps] : pre) {
        auto it = post.find(id);
        if (it == post.end()) continue;
        if (ps.pos != it->second.pos || ps.rot != it->second.rot || ps.scl != it->second.scl)
            { changed = true; break; }
    }
    if (!changed) return;

    m_history.execute({
        pre.size() == 1 ? "Transform" : "Transform " + std::to_string(pre.size()) + " objects",
        [this, post]() {
            for (auto& [id, snap] : post)
                if (auto* o = m_scene.findById(id))
                    { o->position = snap.pos; o->rotation = snap.rot; o->scale = snap.scl; }
        },
        [this, pre]() {
            for (auto& [id, snap] : pre)
                if (auto* o = m_scene.findById(id))
                    { o->position = snap.pos; o->rotation = snap.rot; o->scale = snap.scl; }
        }
    });
}

void App::copySelection()
{
    m_clipboard.clear();
    for (int id : m_scene.selectedIds())
        if (auto* o = m_scene.findById(id))
            m_clipboard.push_back(*o);
}

void App::pasteClipboard()
{
    if (m_clipboard.empty()) return;
    m_scene.selectNone();
    std::vector<int> newIds;

    for (const auto& snap : m_clipboard) {
        SceneObject& obj = m_scene.addObject();
        int newId = obj.id;
        obj    = snap;
        obj.id = newId;
        obj.position.x += 1.0f;
        obj.position.z += 1.0f;
        newIds.push_back(newId);
        m_scene.selectAdd(newId);
    }

    m_history.execute({
        "Paste " + std::to_string(newIds.size()) + " object(s)",
        [](){},
        [this, newIds]() {
            for (int id : newIds) m_scene.removeObject(id);
        }
    });
}

void App::deleteSelection()
{
    std::vector<int> ids = m_scene.selectedIds();
    if (ids.empty()) return;

    std::vector<SceneObject> snapshots;
    for (int id : ids)
        if (auto* o = m_scene.findById(id))
            snapshots.push_back(*o);

    for (int id : ids) m_scene.removeObject(id);

    m_history.execute({
        "Delete " + std::to_string(snapshots.size()) + " object(s)",
        [](){},
        [this, snapshots]() {
            for (const auto& snap : snapshots) {
                SceneObject& o = m_scene.addObject();
                o = snap;
            }
        }
    });
}

void App::addMergedToLibrary(std::shared_ptr<MeshAsset> asset, const std::string& name)
{
    // Add directly to the asset library's entry list
    // sourcePath is empty (procedural/merged mesh — not from disk)
    AssetEntry e;
    e.name        = name;
    e.sourcePath  = "";   // no source file
    e.mesh        = asset;
    e.calibPos    = glm::vec3(0.f);
    e.calibRot    = glm::vec3(0.f);
    e.calibScale  = glm::vec3(1.f);
    e.thumbDirty  = true;

    m_assetLibrary.library().entries().push_back(std::move(e));
    // Note: save on shutdown via AssetLibraryView::shutdown()
    // Merged meshes have no sourcePath so they won't re-import on next load,
    // but they will appear in the library for the current session.

    m_uiState.statusMsg    = "Merged mesh added to Asset Library";
    m_uiState.statusExpiry = glfwGetTime() + 3.0;
}

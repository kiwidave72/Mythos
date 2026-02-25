#include "AssetLibraryView.h"
#include "SceneObject.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>

static constexpr float TILE_SIZE  = 110.f;
static constexpr float THUMB_SIZE =  90.f;
static constexpr int   COLS       =   4;

// ============================================================
// Init / shutdown
// ============================================================

void AssetLibraryView::init(const std::string& jsonPath)
{
    m_jsonPath = jsonPath;
    m_library.load(jsonPath);
    m_thumbRenderer.init();
}

void AssetLibraryView::shutdown()
{
    m_library.save(m_jsonPath);
    m_thumbRenderer.shutdown();
}

// ============================================================
// Selection helpers
// ============================================================

void AssetLibraryView::selectOnly(int idx)
{
    m_selection.clear();
    m_selection.insert(idx);
    m_primaryIdx   = idx;
    m_rangeAnchor  = idx;
}

void AssetLibraryView::toggleSelect(int idx)
{
    if (m_selection.count(idx)) {
        m_selection.erase(idx);
        // Move primary to another selected item if any remain
        if (m_primaryIdx == idx)
            m_primaryIdx = m_selection.empty() ? -1 : *m_selection.begin();
    } else {
        m_selection.insert(idx);
        m_primaryIdx  = idx;
        m_rangeAnchor = idx;
    }
}

void AssetLibraryView::selectRange(int from, int to)
{
    if (from > to) std::swap(from, to);
    for (int i = from; i <= to; ++i)
        m_selection.insert(i);
    m_primaryIdx = to;
}

void AssetLibraryView::clearSelection()
{
    m_selection.clear();
    m_primaryIdx  = -1;
    m_rangeAnchor = -1;
}

// ============================================================
// Apply calibration delta to all selected assets
// ============================================================

void AssetLibraryView::applyCalibToSelection(
    const glm::vec3& dPos,
    const glm::vec3& dRot,
    const glm::vec3& dScl)
{
    for (int idx : m_selection) {
        auto& e = m_library.entries()[idx];
        e.calibPos   += dPos;
        e.calibRot   += dRot;
        e.calibScale += dScl;
        e.thumbDirty  = true;
    }
    m_library.save(m_jsonPath);
}

// ============================================================
// Main draw
// ============================================================

void AssetLibraryView::draw(Scene& scene, std::vector<std::string>& importPaths)
{
    bool justImported = false;
    if (!importPaths.empty()) {
        auto newIndices = m_library.importObjs(importPaths);
        importPaths.clear();
        m_library.save(m_jsonPath);
        m_open        = true;
        justImported  = true;

        // Auto-select all newly imported assets
        clearSelection();
        for (int i : newIndices) {
            m_selection.insert(i);
            m_primaryIdx  = i;
            m_rangeAnchor = i;
        }
    }

    // Render one dirty thumbnail per frame
    for (auto& e : m_library.entries()) {
        if (e.thumbDirty && e.mesh && e.mesh->isLoaded()) {
            m_thumbRenderer.renderThumbnail(e);
            break;
        }
    }

    if (!m_open) return;

    ImGui::SetNextWindowSize({680.f, 500.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({300.f, 70.f},   ImGuiCond_FirstUseEver);
    if (justImported) ImGui::SetNextWindowFocus();

    ImGui::Begin("Asset Library", &m_open);

    if (m_library.count() == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("  No assets imported yet.");
        ImGui::TextDisabled("  Click  Import OBJ...  in the toolbar to add meshes.");
        ImGui::End();
        return;
    }

    // Selection summary in header
    int selCount = (int)m_selection.size();
    if (selCount == 0)
        ImGui::TextDisabled("Click to select  |  Ctrl+Click multi-select  |  Shift+Click range");
    else if (selCount == 1)
        ImGui::TextColored({0.6f,0.85f,1.f,1.f}, "1 asset selected");
    else
        ImGui::TextColored({0.6f,0.85f,1.f,1.f}, "%d assets selected", selCount);

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180.f);
    if (ImGui::SmallButton("Select All")) {
        m_selection.clear();
        for (int i = 0; i < m_library.count(); ++i) m_selection.insert(i);
        m_primaryIdx  = m_library.count() - 1;
        m_rangeAnchor = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) clearSelection();

    ImGui::Separator();

    // Left: grid   Right: calibration panel (when anything selected)
    bool hasPanel = (m_primaryIdx >= 0);
    float panelW = hasPanel ? ImGui::GetContentRegionAvail().x - 230.f
                            : ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("##assetgrid", {panelW, -1.f}, false);
    drawGrid();
    ImGui::EndChild();

    if (hasPanel) {
        ImGui::SameLine();
        ImGui::BeginChild("##calibpanel", {230.f, -1.f}, true);
        drawCalibrationPanel(scene);
        ImGui::EndChild();
    }

    ImGui::End();
}

// ============================================================
// Grid
// ============================================================

void AssetLibraryView::drawGrid()
{
    int n = m_library.count();
    for (int i = 0; i < n; ++i) {
        if (i > 0 && i % COLS != 0)
            ImGui::SameLine();
        drawTile(i);
    }
}

void AssetLibraryView::drawTile(int idx)
{
    auto& e       = m_library.entries()[idx];
    bool  inSel   = isSelected(idx);
    bool  isPrim  = (idx == m_primaryIdx);

    ImGui::PushID(idx);

    // Colour: primary = bright blue, selected = mid blue, unselected = dark
    ImVec4 bg = isPrim  ? ImVec4{0.22f, 0.45f, 0.80f, 0.85f}
              : inSel   ? ImVec4{0.18f, 0.32f, 0.58f, 0.70f}
                        : ImVec4{0.14f, 0.15f, 0.19f, 1.00f};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);

    ImGui::BeginChild("##tile", {TILE_SIZE, TILE_SIZE + 20.f}, true);

    // Thumbnail or grey placeholder
    if (e.thumbnailTex) {
        ImGui::Image((ImTextureID)(uintptr_t)e.thumbnailTex,
                     {THUMB_SIZE, THUMB_SIZE}, {0,1}, {1,0});
    } else {
        ImGui::Dummy({THUMB_SIZE, THUMB_SIZE});
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            IM_COL32(55, 60, 75, 255));
    }

    // Handle click on thumbnail area
    if (ImGui::IsItemClicked()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl) {
            // Ctrl: toggle
            toggleSelect(idx);
        } else if (io.KeyShift && m_rangeAnchor >= 0) {
            // Shift: range from anchor to here
            selectRange(m_rangeAnchor, idx);
            m_primaryIdx = idx;
        } else {
            // Plain click: select only this
            selectOnly(idx);
        }
    }

    // Name label
    std::string label = e.name;
    if (label.size() > 12) label = label.substr(0, 11) + u8"\u2026";
    ImGui::TextUnformatted(label.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", e.name.c_str());

    ImGui::EndChild();

    // Also handle click on the tile border area (EndChild item)
    if (ImGui::IsItemClicked()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)
            toggleSelect(idx);
        else if (io.KeyShift && m_rangeAnchor >= 0) {
            selectRange(m_rangeAnchor, idx);
            m_primaryIdx = idx;
        } else
            selectOnly(idx);
    }

    ImGui::PopStyleColor();
    ImGui::PopID();
}

// ============================================================
// Calibration panel
// ============================================================

void AssetLibraryView::drawCalibrationPanel(Scene& scene)
{
    if (m_primaryIdx < 0 || m_primaryIdx >= m_library.count()) return;

    auto& primary  = m_library.entries()[m_primaryIdx];
    int   selCount = (int)m_selection.size();

    // Header
    if (selCount == 1) {
        ImGui::TextColored({0.9f,0.9f,0.5f,1.f}, "%s", primary.name.c_str());
        if (primary.mesh) {
            auto sz = primary.mesh->data.size();
            ImGui::TextDisabled("%d tris", (int)primary.mesh->data.indices.size()/3);
            ImGui::TextDisabled("%.2f x %.2f x %.2f", sz.x, sz.y, sz.z);
        }
    } else {
        ImGui::TextColored({0.6f,0.85f,1.f,1.f}, "%d assets", selCount);
        ImGui::TextDisabled("Changes apply to all.");
    }

    ImGui::Separator();
    ImGui::Spacing();

    // ---- Calibration controls ----
    // For multi-select we show the PRIMARY's values as reference,
    // but drag deltas are broadcast to all selected.
    ImGui::Text("Calibration");
    if (selCount > 1)
        ImGui::TextDisabled("Dragging edits all %d.", selCount);
    ImGui::Spacing();

    // We use a snapshot + delta approach so multi-select works correctly:
    // capture the primary value before the widget, compare after.
    {
        glm::vec3 pos = primary.calibPos;
        ImGui::Text("Offset");
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##cpos", &pos.x, 0.01f)) {
            glm::vec3 d = pos - primary.calibPos;
            if (selCount == 1) { primary.calibPos = pos; primary.thumbDirty = true; m_library.save(m_jsonPath); }
            else                applyCalibToSelection(d, {0,0,0}, {0,0,0});
        }
    }
    {
        glm::vec3 rot = primary.calibRot;
        ImGui::Text("Rotation (deg)");
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##crot", &rot.x, 0.5f)) {
            glm::vec3 d = rot - primary.calibRot;
            if (selCount == 1) { primary.calibRot = rot; primary.thumbDirty = true; m_library.save(m_jsonPath); }
            else                applyCalibToSelection({0,0,0}, d, {0,0,0});
        }
    }
    {
        glm::vec3 scl = primary.calibScale;
        ImGui::Text("Scale");
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::DragFloat3("##cscl", &scl.x, 0.01f, 0.001f, 100.f)) {
            glm::vec3 d = scl - primary.calibScale;
            if (selCount == 1) { primary.calibScale = scl; primary.thumbDirty = true; m_library.save(m_jsonPath); }
            else                applyCalibToSelection({0,0,0}, {0,0,0}, d);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Presets (apply to all):");
    if (ImGui::Button("Reset",        {-1,0})) {
        for (int i : m_selection) {
            auto& e = m_library.entries()[i];
            e.calibPos = {0,0,0}; e.calibRot = {0,0,0}; e.calibScale = {1,1,1};
            e.thumbDirty = true;
        }
        m_library.save(m_jsonPath);
    }
    if (ImGui::Button("Z-up → Y-up", {-1,0})) {
        for (int i : m_selection) { m_library.entries()[i].calibRot = {-90,0,0}; m_library.entries()[i].thumbDirty = true; }
        m_library.save(m_jsonPath);
    }
    if (ImGui::Button("-Z forward",   {-1,0})) {
        for (int i : m_selection) { m_library.entries()[i].calibRot = {0,180,0}; m_library.entries()[i].thumbDirty = true; }
        m_library.save(m_jsonPath);
    }

    // ---- Add to Scene ----
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f,0.55f,0.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f,0.72f,0.22f,1.f});
    std::string addLabel = selCount > 1
        ? ("Add " + std::to_string(selCount) + " to Scene")
        : "Add to Scene";
    if (ImGui::Button(addLabel.c_str(), {-1.f, 36.f})) {
        // Place in a simple grid so they don't all stack on origin
        int col = 0;
        float spacing = 3.f;
        for (int i : m_selection) {
            placeIntoScene(scene, i);
            // Offset each placed object so they're visible
            auto* obj = scene.findById(scene.selectedId());
            if (obj) {
                obj->position.x += (col % 4) * spacing;
                obj->position.z += (col / 4) * spacing;
            }
            ++col;
        }
    }
    ImGui::PopStyleColor(2);

    // ---- Remove ----
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.45f,0.12f,0.12f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.65f,0.18f,0.18f,1.f});
    std::string removeLabel = selCount > 1
        ? ("Remove " + std::to_string(selCount) + " from Library")
        : "Remove from Library";
    if (ImGui::Button(removeLabel.c_str(), {-1.f, 0.f})) {
        // Remove in reverse index order so indices stay valid
        std::vector<int> toRemove(m_selection.rbegin(), m_selection.rend());
        for (int i : toRemove)
            m_library.remove(i);
        m_library.save(m_jsonPath);
        clearSelection();
    }
    ImGui::PopStyleColor(2);
}

// ============================================================
// Place into scene
// ============================================================

void AssetLibraryView::placeIntoScene(Scene& scene, int assetIdx)
{
    if (assetIdx < 0 || assetIdx >= m_library.count()) return;
    auto& e = m_library.entries()[assetIdx];
    if (!e.mesh) return;

    SceneObject& obj = scene.addObject();
    obj.name   = e.name;
    obj.primId = e.name;
    obj.mesh   = e.mesh;
    obj.color  = {0.75f, 0.78f, 0.85f};

    // Apply calibration as base
    obj.position = e.calibPos;
    obj.rotation = e.calibRot;
    obj.scale    = e.calibScale;

    // Auto-scale to fit one grid cell on placement.
    // Use the longest XZ extent of the AABB so the asset fills exactly kGridCell
    // units on its widest axis. Y is scaled proportionally.
    const glm::vec3 sz = e.mesh->data.size();
    float xzMax = std::max(sz.x, sz.z);
    if (xzMax > 1e-4f) {
        float uniformScale = kGridCell / xzMax;
        obj.scale = e.calibScale * uniformScale;
    }

    // Sit asset on the ground plane (Y=0): offset by half the scaled height
    // so the bottom face lands on the grid rather than the origin.
    float scaledHalfH = (sz.y * obj.scale.y) * 0.5f;
    obj.position.y = scaledHalfH;

    scene.selectById(obj.id);
    std::cout << "[AssetLibraryView] Placed '" << e.name
              << "' scale=" << obj.scale.x
              << " (id=" << obj.id << ")\n";
}

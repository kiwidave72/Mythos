#pragma once
#include "AssetLibrary.h"
#include "ThumbnailRenderer.h"
#include "Scene.h"
#include <string>
#include <set>

class AssetLibraryView
{
public:
    void init(const std::string& libraryJsonPath);
    void shutdown();

    void draw(Scene& scene, std::vector<std::string>& importPaths);

    AssetLibrary& library() { return m_library; }

    bool isOpen()           const { return m_open; }
    void setOpen(bool open)       { m_open = open; }

private:
    AssetLibrary      m_library;
    ThumbnailRenderer m_thumbRenderer;
    std::string       m_jsonPath;
    bool              m_open = true;

    // Multi-selection state
    std::set<int> m_selection;     // all selected indices
    int           m_primaryIdx = -1;   // last clicked — drives calibration display
    int           m_rangeAnchor = -1;  // shift-click anchor

    void drawGrid();
    void drawTile(int idx);
    void drawCalibrationPanel(Scene& scene);
    void placeIntoScene(Scene& scene, int assetIdx);

    // Selection helpers
    bool isSelected(int idx) const { return m_selection.count(idx) > 0; }
    void selectOnly(int idx);
    void toggleSelect(int idx);
    void selectRange(int from, int to);
    void clearSelection();

    // Apply a calibration delta to all selected assets
    void applyCalibToSelection(
        const glm::vec3& dPos,
        const glm::vec3& dRot,
        const glm::vec3& dScl);
};

#pragma once
#include <GLFW/glfw3.h>

// InputRouter decides who owns input each frame.
//
// RULE:
//   - If the mouse is hovering over any ImGui window → ImGui owns keyboard AND mouse.
//   - If the mouse is in the 3D viewport (not over any ImGui window) → Scene owns input.
//
// GLFW callbacks fire first. We check ImGui's WantCaptureMouse /
// WantCaptureKeyboard flags AFTER ImGui::NewFrame() has been called
// (that's when they're accurate for the current frame).
//
// Callers ask "does the scene own this?" before acting on input.

class InputRouter
{
public:
    // Call once per frame after ImGui::NewFrame()
    void update();

    // Scene should only act on mouse input when this returns true
    bool sceneOwnsMouse()    const { return m_sceneOwnsMouse; }

    // Scene should only act on keyboard input when this returns true
    bool sceneOwnsKeyboard() const { return m_sceneOwnsKeyboard; }

private:
    bool m_sceneOwnsMouse    = true;
    bool m_sceneOwnsKeyboard = true;
};

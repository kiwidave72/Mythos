#include "InputRouter.h"
#include <imgui.h>

void InputRouter::update()
{
    ImGuiIO& io = ImGui::GetIO();

    // ImGui sets these flags based on where the mouse is and what it's doing.
    // WantCaptureMouse  = mouse is over an ImGui window or ImGui is dragging something
    // WantCaptureKeyboard = an ImGui widget (e.g. InputText) has keyboard focus
    m_sceneOwnsMouse    = !io.WantCaptureMouse;
    m_sceneOwnsKeyboard = !io.WantCaptureKeyboard;
}

# GraphEditor — Build Instructions

## Requirements

- CMake 3.20+
- Visual Studio 2022 (MSVC)
- vcpkg with the following packages:

```
vcpkg install glfw3:x64-windows
vcpkg install glad:x64-windows
vcpkg install glm:x64-windows
vcpkg install imgui[glfw-binding,opengl3-binding,docking-experimental]:x64-windows
```

> The `docking-experimental` feature is required — it enables ImGui docking
> which the editor uses for its panel layout.

## Build

```bat
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/github/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

## Run

```bat
build\Release\GraphEditor.exe
```

**Important:** The executable must run from the project root so it can find
`shaders/mesh.vert`, `shaders/mesh.frag`, `shaders/grid.vert`, `shaders/grid.frag`.
CMake copies these automatically to the build directory, so running from
`build\Release\` also works.

## Controls

| Input | Action |
|-------|--------|
| Left drag (scene) | Orbit camera |
| Right drag (scene) | Pan camera |
| Scroll (scene) | Zoom |
| P | Toggle play/editor mode |
| F | Frame origin (reset camera target) |
| ESC | Exit play mode / quit |

## Input Routing

The stats overlay (top-right) shows who owns mouse and keyboard in real time:

- **Mouse: Scene** — orbit/pan/zoom active
- **Mouse: ImGui** — hovering an ImGui window; scene ignores mouse
- **Keys: Scene** — P, F, etc. are active
- **Keys: ImGui** — an editbox or widget has focus; scene ignores keyboard

This is the fundamental mechanism for mixing a game loop with an editor UI.
The Test Input Routing window demonstrates it — focus the editbox and watch
the Keys indicator switch to ImGui.

## Architecture

```
src/
  main.cpp        Entry point
  App.h/.cpp      Window, game loop, GLFW callbacks, input routing decisions
  Renderer.h/.cpp Modern GL (VAO/VBO/shaders): cube mesh, grid
  EditorUI.h/.cpp All ImGui panels: toolbar, stats overlay, test window
  InputRouter.h/.cpp  WantCaptureMouse / WantCaptureKeyboard logic

shaders/
  mesh.vert/.frag   Blinn-Phong lit mesh
  grid.vert/.frag   Flat-colour grid lines
```

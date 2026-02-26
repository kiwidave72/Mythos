cmake_minimum_required(VERSION 3.20)
project(Mythos CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies via vcpkg or find_package ----
find_package(glfw3  CONFIG REQUIRED)
find_package(glad   CONFIG REQUIRED)
find_package(glm    CONFIG REQUIRED)
find_package(imgui    CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
# imnodes vendored in third_party/imnodes/ — no find_package needed

# ---- Shaders copied to build dir ----
file(GLOB SHADER_FILES "${CMAKE_SOURCE_DIR}/shaders/*.vert"
                       "${CMAKE_SOURCE_DIR}/shaders/*.frag")
foreach(SHADER ${SHADER_FILES})
    get_filename_component(FNAME ${SHADER} NAME)
    configure_file(${SHADER} "${CMAKE_BINARY_DIR}/shaders/${FNAME}" COPYONLY)
endforeach()

# ---- Sources ----
set(SOURCES
    src/main.cpp
    src/App.cpp
    src/Renderer.cpp
    src/EditorUI.cpp
    src/InputRouter.cpp
    src/Scene.cpp
    src/MeshAsset.cpp
    src/MeshMerge.cpp
    src/ObjImporter.cpp
    src/GltfImporter.cpp
    src/ProjectFile.cpp
    src/FileDialog.cpp
    src/AssetLibrary.cpp
    src/AssetLibraryView.cpp
    src/ThumbnailRenderer.cpp
    # grammar-core: pure logic, zero GL/ImGui
    lib/grammar-core/Grammar.cpp
    lib/grammar-core/GrammarInducer.cpp
    lib/grammar-core/HalfEdgeMesh.cpp
    # merrell DPO grammar — MG-0 data structures (MG-1 through MG-4 implement the TODOs)
    lib/grammar-core/MerrellGraph.cpp
    lib/grammar-core/DPORule.cpp
    lib/grammar-core/MerrellGrammar.cpp
    # grammar-ui: ImGui panels for grammar editing
    lib/grammar-ui/GrammarView.cpp
    lib/grammar-ui/GraphViewer.cpp
    # imnodes: vendored in-tree, bypasses vcpkg
    third_party/imnodes/imnodes.cpp
)

add_executable(Mythos ${SOURCES})

# Set as default startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Mythos)

# Enable GLM experimental extensions (required for glm::decompose)
target_compile_definitions(Mythos PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_include_directories(Mythos PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/grammar-core
    ${CMAKE_SOURCE_DIR}/lib/grammar-ui
    ${CMAKE_SOURCE_DIR}/third_party/imnodes
)

# vcpkg puts imgui backends headers here
target_include_directories(Mythos PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-windows/include
    ${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-windows/include/imgui
)

target_link_libraries(Mythos PRIVATE
    glfw
    glad::glad
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
)

# Windows: link comdlg32 for native file dialogs (GetOpenFileName)
if(WIN32)
    target_link_libraries(Mythos PRIVATE comdlg32)
endif()

# Windows: subsystem console so we get stdout for debug prints
if(WIN32)
    set_target_properties(Mythos PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE")
endif()

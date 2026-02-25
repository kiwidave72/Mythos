cmake_minimum_required(VERSION 3.20)
project(GraphEditor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies via vcpkg or find_package ----
find_package(glfw3  CONFIG REQUIRED)
find_package(glad   CONFIG REQUIRED)
find_package(glm    CONFIG REQUIRED)
find_package(imgui    CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

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
    # grammar-ui: ImGui panels for grammar editing
    lib/grammar-ui/GrammarView.cpp
)

add_executable(GraphEditor ${SOURCES})

# Enable GLM experimental extensions (required for glm::decompose)
target_compile_definitions(GraphEditor PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_include_directories(GraphEditor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/grammar-core
    ${CMAKE_SOURCE_DIR}/lib/grammar-ui
)

# vcpkg puts imgui backends headers here
target_include_directories(GraphEditor PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-windows/include
    ${CMAKE_CURRENT_LIST_DIR}/vcpkg_installed/x64-windows/include/imgui
)

target_link_libraries(GraphEditor PRIVATE
    glfw
    glad::glad
    glm::glm
    imgui::imgui
    imguizmo::imguizmo
)

# Windows: link comdlg32 for native file dialogs (GetOpenFileName)
if(WIN32)
    target_link_libraries(GraphEditor PRIVATE comdlg32)
endif()

# Windows: subsystem console so we get stdout for debug prints
if(WIN32)
    set_target_properties(GraphEditor PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE")
endif()

if(WIN32)
    # Expect version like: 1.2.3 or 1.2.3-beta.4
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${MYTHOS_VERSION_SEMVER}")

    set(MYTHOS_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(MYTHOS_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(MYTHOS_VERSION_PATCH "${CMAKE_MATCH_3}")

    set(MYTHOS_VERSION_COMMA
        "${MYTHOS_VERSION_MAJOR},${MYTHOS_VERSION_MINOR},${MYTHOS_VERSION_PATCH},0")

    configure_file(
        ${CMAKE_SOURCE_DIR}/version.rc.in
        ${CMAKE_BINARY_DIR}/version.rc
        @ONLY
    )
endif()

if(WIN32)
    # This points to the file created in your CMAKE_BINARY_DIR
    set(RES_FILES "${CMAKE_BINARY_DIR}/version.rc")
endif()

# Add the resource file here!
add_executable(Mythos 
    main.cpp 
    ${RES_FILES} 
)
#pragma once
// Grammar library — no GL, no ImGui, no GLFW.
// This file is the only dependency the generator needs.
// Rendering is the editor's concern, not the library's.

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <glm/glm.hpp>

namespace grammar {

// ---- Socket ----------------------------------------------------------------
// A grid-space connection point on a primitive.
// gridDir: which cell face this socket is on  (-1,0)=left  (1,0)=right
//                                              (0,-1)=top   (0,1)=bottom
// When physical mesh import arrives, add worldPos + worldNorm here
// for 3D connector snapping — the generator never reads those fields.
struct Socket {
    glm::ivec2 gridDir;
};

// ---- PrimDef ---------------------------------------------------------------
// Describes a primitive type: its sockets and visual colour.
// draw is set by the editor/renderer at registration time — the lib never calls it.
struct PrimDef {
    std::string              id;
    glm::vec3                color;
    std::vector<Socket>      sockets;      // exactly 2 for current piece types
    std::function<void()>    draw;         // set by editor, never touched by lib
};

// ---- Placed ----------------------------------------------------------------
// One instance of a PrimDef placed in the grid.
struct Placed {
    const PrimDef* def  = nullptr;
    glm::ivec2     cell = {0, 0};
    int            rot  = 0;       // reserved for future rotation support
};

// ---- GeneratorState --------------------------------------------------------
// Exposed so the editor can render mid-generation for animation.
struct GeneratorState {
    int  attempt    = 0;       // current attempt number (0-based)
    int  maxAttempt = 2000;
    bool running    = false;
    bool success    = false;
    bool failed     = false;

    // Live path being built this attempt (for animated preview)
    std::vector<Placed> livePath;
    glm::ivec2          curCell  = {0,0};
    glm::ivec2          curDir   = {1,0};
};

// ---- Grammar ---------------------------------------------------------------
class Grammar {
public:
    // --- Library registration ---
    void addPrim(const char* id,
                 glm::vec3   color,
                 std::vector<glm::ivec2> socketDirs,
                 std::function<void()>   drawFn = {});

    const PrimDef* findPrim(const char* id) const;

    // --- Result ---
    // Finalised placed pieces after a successful generate()
    std::vector<Placed>              placed;
    std::map<std::pair<int,int>,int> grid;   // cell -> index into placed

    // --- Settings ---
    int  maxPrim   = 60;   // upper bound on path length
    int  minPrim   = 16;   // lower bound — loop must use at least this many pieces
                           // so there is guaranteed interior space.
                           // Minimum sensible value is 8 (a 3×1 rectangle needs 8 pieces).
                           // 16 gives a loop that encloses roughly a 3×3 interior.
    int  seed      = 42;
    bool hardcoded = false;

    // --- Generation ---
    // Blocking — runs all attempts, fills placed/grid on success.
    // progressCb called each attempt with (attempt, maxAttempt).
    void generate(std::function<void(int,int)> progressCb = {});

    // Step-based for animated generation — call from editor each frame.
    // Returns true when done (success or all attempts exhausted).
    bool stepGenerate();         // advances one attempt per call
    void beginGenerate();        // resets state, call once before stepping
    const GeneratorState& state() const { return m_state; }

    // --- Serialise ---
    std::string encode() const;
    bool        decode(const std::string& s);

    // --- Helpers ---
    bool cellFree(glm::ivec2 c) const;

private:
    std::vector<PrimDef> m_lib;

    GeneratorState m_state;

    // Shared generation logic for one attempt
    bool runAttempt(int attempt);

    // Hardcoded demo layout
    void generateHardcoded();

    static int       turnSign (glm::ivec2 in, glm::ivec2 out);
    static glm::ivec2 getOutDir(const PrimDef* def, glm::ivec2 inDir);
};

} // namespace grammar

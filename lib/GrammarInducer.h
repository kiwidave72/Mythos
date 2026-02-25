#pragma once
// GrammarInducer — learns a tile grammar from a hand-crafted example GEP scene.
//
// Pipeline:
//   GEP file  →  InducedGrammar  →  stored back in GEP "inducedGrammar" block
//
// The InducedGrammar contains:
//   - TileType registry  : each unique (assetName) with its observed socket faces
//                          per rotation variant
//   - CompatibilityRules : which (tile,rot) can sit in direction D next to (tile,rot)
//   - ExampleGraph       : the adjacency graph of the original example scene
//
// Nothing in this file depends on OpenGL, ImGui, or GLFW.
// The JSON parser is the same minimal one used by GltfImporter / ProjectFile.

#include <string>
#include <vector>
#include <map>
#include <set>
#include <glm/glm.hpp>

namespace grammar {

// ---- Direction helpers -----------------------------------------------------

enum class Dir { N, S, E, W };

inline Dir opposite(Dir d) {
    switch(d) {
        case Dir::N: return Dir::S;
        case Dir::S: return Dir::N;
        case Dir::E: return Dir::W;
        case Dir::W: return Dir::E;
    }
    return Dir::N;
}
inline std::string dirName(Dir d) {
    switch(d){ case Dir::N:return "N"; case Dir::S:return "S";
               case Dir::E:return "E"; case Dir::W:return "W"; }
    return "?";
}
inline glm::ivec2 dirVec(Dir d) {
    switch(d){ case Dir::N:return{ 0,-1}; case Dir::S:return{ 0, 1};
               case Dir::E:return{ 1, 0}; case Dir::W:return{-1, 0}; }
    return {0,0};
}
inline Dir dirFromVec(glm::ivec2 v) {
    if(v.x== 1&&v.y== 0) return Dir::E;
    if(v.x==-1&&v.y== 0) return Dir::W;
    if(v.x== 0&&v.y== 1) return Dir::S;
    return Dir::N;
}

// ---- Data structures -------------------------------------------------------

// A tile variant = one asset at one rotation (normalised to 0/90/180/270).
struct TileVariant {
    std::string  assetName;     // e.g. "square_forest_roadC.gltf"
    std::string  meshSource;    // full path to .glb/.gltf file
    int          rotation = 0;  // 0 / 90 / 180 / 270
    std::vector<Dir> openFaces; // faces with a socket (inferred from adjacency)

    bool operator==(const TileVariant& o) const {
        return assetName == o.assetName && rotation == o.rotation;
    }
};

// A compatibility rule: tile A at rotation rA, facing direction dir,
// can be adjacent to tile B at rotation rB.
struct CompatRule {
    std::string fromAsset;
    int         fromRot;
    Dir         dir;
    std::string toAsset;
    int         toRot;
};

// A node in the example graph.
struct GraphNode {
    int         id;
    std::string assetName;
    std::string meshSource;
    int         rotation;       // normalised
    glm::ivec2  gridPos;
    float       scale;          // uniform scale used in example
};

// A directed edge in the example graph.
struct GraphEdge {
    int fromId;
    int toId;
    Dir dir;
};

// The full induced grammar.
struct InducedGrammar {
    // Source file this was induced from
    std::string sourceGep;

    // All unique tile variants observed
    std::vector<TileVariant>  tileVariants;

    // All pairwise compatibility rules
    std::vector<CompatRule>   rules;

    // The example graph (nodes + edges)
    std::vector<GraphNode>    nodes;
    std::vector<GraphEdge>    edges;

    // Quick lookup: can (fromAsset,fromRot) connect in dir to (toAsset,toRot)?
    bool isCompatible(const std::string& fromAsset, int fromRot, Dir dir,
                      const std::string& toAsset,   int toRot) const;

    // All variants that can connect to (asset, rot) from direction dir
    // (i.e. variants that have opposite(dir) open, and a matching rule exists)
    std::vector<const TileVariant*> candidatesFor(
        const std::string& asset, int rot, Dir dir) const;

    // Serialise to JSON string (for embedding in GEP file)
    std::string toJson() const;
};

// ---- Inducer ---------------------------------------------------------------

class GrammarInducer {
public:
    // Induce a grammar from a GEP JSON string.
    // Returns empty grammar (nodes.empty()) on failure.
    static InducedGrammar induce(const std::string& gepJson);

    // Convenience: load from file path.
    static InducedGrammar induceFromFile(const std::string& path);

    // Last error message.
    static const std::string& lastError() { return s_error; }

private:
    static std::string s_error;

    static int  normaliseRot(float degrees);
};

} // namespace grammar

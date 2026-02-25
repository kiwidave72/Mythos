#include "GrammarInducer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace grammar {

std::string GrammarInducer::s_error;

// ============================================================
// Minimal JSON parser (same pattern as GltfImporter/ProjectFile)
// ============================================================

struct JV;
using JA = std::vector<JV>;
using JO = std::vector<std::pair<std::string,JV>>;

struct JV {
    enum { Null,Bool,Num,Str,Arr,Obj } type = Null;
    bool b=false; double n=0; std::string s;
    std::shared_ptr<JA> arr;
    std::shared_ptr<JO> obj;

    double      num()     const { return type==Num ?n:0; }
    int         inum()    const { return (int)num(); }
    std::string str()     const { return type==Str ?s:""; }
    bool        boolean() const { return type==Bool?b:false; }

    const JV& operator[](const std::string& k) const {
        static JV null;
        if(type!=Obj) return null;
        for(auto& kv:*obj) if(kv.first==k) return kv.second;
        return null;
    }
    const JV& operator[](size_t i) const {
        static JV null;
        if(type!=Arr||i>=arr->size()) return null;
        return (*arr)[i];
    }
    size_t size() const {
        if(type==Arr) return arr->size();
        if(type==Obj) return obj->size();
        return 0;
    }
    bool has(const std::string& k) const {
        if(type!=Obj) return false;
        for(auto& kv:*obj) if(kv.first==k) return true;
        return false;
    }
};

struct JP {
    const char* p; const char* end;
    JP(const char* d,size_t l):p(d),end(d+l){}
    void ws(){while(p<end&&(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'))++p;}
    JV parse(){
        ws(); if(p>=end) return {};
        char c=*p;
        if(c=='{') return pObj();
        if(c=='[') return pArr();
        if(c=='"') return pStr();
        if(c=='t'){p+=4;JV v;v.type=JV::Bool;v.b=true; return v;}
        if(c=='f'){p+=5;JV v;v.type=JV::Bool;v.b=false;return v;}
        if(c=='n'){p+=4;return {};}
        return pNum();
    }
    JV pObj(){
        ++p; JV v; v.type=JV::Obj; v.obj=std::make_shared<JO>();
        ws(); while(p<end&&*p!='}'){
            if(*p==','){++p;ws();continue;}
            JV k=pStr(); ws(); if(p<end&&*p==':')++p;
            v.obj->push_back({k.s,parse()}); ws();
        }
        if(p<end)++p; return v;
    }
    JV pArr(){
        ++p; JV v; v.type=JV::Arr; v.arr=std::make_shared<JA>();
        ws(); while(p<end&&*p!=']'){
            if(*p==','){++p;ws();continue;}
            v.arr->push_back(parse()); ws();
        }
        if(p<end)++p; return v;
    }
    JV pStr(){
        if(*p=='"')++p; JV v; v.type=JV::Str;
        while(p<end&&*p!='"'){
            if(*p=='\\'){++p;if(p<end){char c=*p++;
                if(c=='n')v.s+='\n'; else if(c=='r')v.s+='\r';
                else if(c=='t')v.s+='\t'; else v.s+=c;
            }}
            else v.s+=*p++;
        }
        if(p<end)++p; return v;
    }
    JV pNum(){
        const char* s=p;
        if(*p=='-')++p;
        while(p<end&&(std::isdigit((unsigned char)*p)||*p=='.'||
                       *p=='e'||*p=='E'||*p=='+'||*p=='-'))++p;
        JV v; v.type=JV::Num;
        v.n=std::stod(std::string(s,p)); return v;
    }
};

// ============================================================
// Helpers
// ============================================================

int GrammarInducer::normaliseRot(float degrees)
{
    // Handle large accumulated rotation values (e.g. 18027090 from ImGuizmo drift)
    float wrapped = std::fmod(degrees, 360.f);
    if (wrapped < 0.f) wrapped += 360.f;
    // Snap to nearest 90°
    int snapped = (int)std::round(wrapped / 90.f) * 90;
    return snapped % 360;
}

static std::string escape(const std::string& s)
{
    std::string o; o.reserve(s.size()+4);
    for(char c:s){
        if     (c=='"')  o+="\\\"";
        else if(c=='\\') o+="\\\\";
        else if(c=='\n') o+="\\n";
        else if(c=='\r') o+="\\r";
        else             o+=c;
    }
    return o;
}

static std::string assetShortName(const std::string& meshName)
{
    // meshName is "gltf:square_forest_roadC.gltf" → "square_forest_roadC.gltf"
    // or it's the raw name field from the object
    if (meshName.substr(0,5) == "gltf:") return meshName.substr(5);
    return meshName;
}

// ============================================================
// InducedGrammar methods
// ============================================================

bool InducedGrammar::isCompatible(const std::string& fromAsset, int fromRot, Dir dir,
                                   const std::string& toAsset,   int toRot) const
{
    for (auto& r : rules)
        if (r.fromAsset == fromAsset && r.fromRot == fromRot &&
            r.dir == dir &&
            r.toAsset == toAsset && r.toRot == toRot)
            return true;
    return false;
}

std::vector<const TileVariant*> InducedGrammar::candidatesFor(
    const std::string& asset, int rot, Dir dir) const
{
    std::vector<const TileVariant*> result;
    for (auto& r : rules) {
        if (r.fromAsset != asset || r.fromRot != rot || r.dir != dir)
            continue;
        // Find the matching variant
        for (auto& v : tileVariants)
            if (v.assetName == r.toAsset && v.rotation == r.toRot) {
                result.push_back(&v);
                break;
            }
    }
    return result;
}

std::string InducedGrammar::toJson() const
{
    std::ostringstream o;
    o << "{\n";
    o << "  \"sourceGep\": \"" << escape(sourceGep) << "\",\n";

    // Tile variants
    o << "  \"tileVariants\": [\n";
    for (size_t i = 0; i < tileVariants.size(); ++i) {
        auto& v = tileVariants[i];
        o << "    {\n";
        o << "      \"assetName\": \""  << escape(v.assetName)  << "\",\n";
        o << "      \"meshSource\": \"" << escape(v.meshSource) << "\",\n";
        o << "      \"rotation\": "     << v.rotation           << ",\n";
        o << "      \"openFaces\": [";
        for (size_t j = 0; j < v.openFaces.size(); ++j) {
            o << "\"" << dirName(v.openFaces[j]) << "\"";
            if (j+1 < v.openFaces.size()) o << ",";
        }
        o << "]\n";
        o << "    }" << (i+1<tileVariants.size()?",":"") << "\n";
    }
    o << "  ],\n";

    // Compatibility rules
    o << "  \"rules\": [\n";
    for (size_t i = 0; i < rules.size(); ++i) {
        auto& r = rules[i];
        o << "    {\"from\":\"" << escape(r.fromAsset) << "\""
          << ",\"fromRot\":"    << r.fromRot
          << ",\"dir\":\""      << dirName(r.dir) << "\""
          << ",\"to\":\""       << escape(r.toAsset) << "\""
          << ",\"toRot\":"      << r.toRot
          << "}" << (i+1<rules.size()?",":"") << "\n";
    }
    o << "  ],\n";

    // Example graph nodes
    o << "  \"exampleGraph\": {\n";
    o << "    \"nodes\": [\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto& n = nodes[i];
        o << "      {\"id\":"        << n.id
          << ",\"assetName\":\""     << escape(n.assetName) << "\""
          << ",\"meshSource\":\""    << escape(n.meshSource) << "\""
          << ",\"rotation\":"        << n.rotation
          << ",\"gridPos\":["        << n.gridPos.x << "," << n.gridPos.y << "]"
          << ",\"scale\":"           << n.scale
          << "}" << (i+1<nodes.size()?",":"") << "\n";
    }
    o << "    ],\n";

    // Example graph edges
    o << "    \"edges\": [\n";
    for (size_t i = 0; i < edges.size(); ++i) {
        auto& e = edges[i];
        o << "      {\"from\":" << e.fromId
          << ",\"to\":"         << e.toId
          << ",\"dir\":\""      << dirName(e.dir) << "\""
          << "}" << (i+1<edges.size()?",":"") << "\n";
    }
    o << "    ]\n";
    o << "  }\n";
    o << "}";
    return o.str();
}

// ============================================================
// Induction
// ============================================================

InducedGrammar GrammarInducer::induceFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f) {
        s_error = "Cannot open: " + path;
        std::cerr << "[GrammarInducer] " << s_error << "\n";
        return {};
    }
    std::string json((std::istreambuf_iterator<char>(f)), {});
    return induce(json);
}

InducedGrammar GrammarInducer::induce(const std::string& gepJson)
{
    InducedGrammar result;
    s_error.clear();

    JP parser(gepJson.data(), gepJson.size());
    JV root = parser.parse();
    if (root.type != JV::Obj) {
        s_error = "Invalid GEP JSON";
        return result;
    }

    const JV& objArr = root["objects"];
    if (objArr.type != JV::Arr || objArr.size() == 0) {
        s_error = "No objects in GEP file";
        return result;
    }

    // ---- Step 1: Build grid map ----
    // key = (x, z) in grid units (rounded from world position)
    struct ObjInfo {
        int         id;
        std::string assetName;
        std::string meshSource;
        int         rotation;   // normalised
        float       scale;
        glm::ivec2  gridPos;
    };

    std::map<std::pair<int,int>, ObjInfo> grid;
    std::map<int, ObjInfo> byId;

    for (size_t i = 0; i < objArr.size(); ++i) {
        const JV& jo = objArr[i];
        int id = jo["id"].inum();

        // World position → grid cell (round to nearest integer)
        float wx = (float)jo["position"][0].num();
        float wz = (float)jo["position"][2].num();
        int gx = (int)std::round(wx);
        int gz = (int)std::round(wz);

        // Rotation: use Y component, normalise
        float rotY = (float)jo["rotation"][1].num();
        int   rot  = normaliseRot(rotY);

        // Asset name: prefer meshName (stripped), fall back to name field
        std::string meshName   = jo["meshName"].str();
        std::string assetName  = assetShortName(meshName.empty() ? jo["name"].str() : meshName);
        std::string meshSource = jo["meshSource"].str();

        // Scale: read x component of scale array
        float scale = (float)jo["scale"][0].num();

        ObjInfo info{ id, assetName, meshSource, rot, scale, {gx, gz} };
        grid[{gx, gz}] = info;
        byId[id]        = info;
    }

    if (grid.empty()) {
        s_error = "No valid objects with grid positions";
        return result;
    }

    std::cout << "[GrammarInducer] Grid: " << grid.size() << " tiles\n";

    // ---- Step 2: Infer open faces per (assetName, rotation) ----
    static const Dir allDirs[] = { Dir::N, Dir::S, Dir::E, Dir::W };

    // openFaces[(assetName,rot)] = set of open face directions
    std::map<std::pair<std::string,int>, std::set<Dir>> openFaces;

    for (auto& [pos, obj] : grid) {
        auto key = std::make_pair(obj.assetName, obj.rotation);
        for (Dir d : allDirs) {
            glm::ivec2 nbPos = { pos.first + dirVec(d).x,
                                 pos.second + dirVec(d).y };
            if (grid.count({nbPos.x, nbPos.y})) {
                openFaces[key].insert(d);
            }
        }
    }

    // ---- Step 3: Build TileVariant list ----
    for (auto& [key, faces] : openFaces) {
        TileVariant tv;
        tv.assetName = key.first;
        tv.rotation  = key.second;
        // Get meshSource from any object with this assetName
        for (auto& [pos, obj] : grid)
            if (obj.assetName == key.first) { tv.meshSource = obj.meshSource; break; }
        for (Dir d : allDirs)
            if (faces.count(d)) tv.openFaces.push_back(d);
        result.tileVariants.push_back(tv);
    }

    // Sort for determinism
    std::sort(result.tileVariants.begin(), result.tileVariants.end(),
        [](const TileVariant& a, const TileVariant& b){
            if (a.assetName != b.assetName) return a.assetName < b.assetName;
            return a.rotation < b.rotation;
        });

    // ---- Step 4: Build compatibility rules from observed adjacencies ----
    std::set<std::tuple<std::string,int,Dir,std::string,int>> ruleSet;

    for (auto& [pos, obj] : grid) {
        for (Dir d : allDirs) {
            glm::ivec2 nbPos = { pos.first + dirVec(d).x,
                                 pos.second + dirVec(d).y };
            auto it = grid.find({nbPos.x, nbPos.y});
            if (it == grid.end()) continue;

            auto& nb = it->second;
            auto key = std::make_tuple(obj.assetName, obj.rotation, d,
                                       nb.assetName,  nb.rotation);
            if (!ruleSet.count(key)) {
                ruleSet.insert(key);
                CompatRule r;
                r.fromAsset = obj.assetName;
                r.fromRot   = obj.rotation;
                r.dir       = d;
                r.toAsset   = nb.assetName;
                r.toRot     = nb.rotation;
                result.rules.push_back(r);
            }
        }
    }

    // Sort rules for determinism
    std::sort(result.rules.begin(), result.rules.end(),
        [](const CompatRule& a, const CompatRule& b){
            if (a.fromAsset != b.fromAsset) return a.fromAsset < b.fromAsset;
            if (a.fromRot   != b.fromRot)   return a.fromRot   < b.fromRot;
            if (a.dir       != b.dir)        return a.dir       < b.dir;
            if (a.toAsset   != b.toAsset)    return a.toAsset   < b.toAsset;
            return a.toRot < b.toRot;
        });

    // ---- Step 5: Build example graph ----
    for (auto& [pos, obj] : grid) {
        GraphNode n;
        n.id         = obj.id;
        n.assetName  = obj.assetName;
        n.meshSource = obj.meshSource;
        n.rotation   = obj.rotation;
        n.gridPos    = obj.gridPos;
        n.scale      = obj.scale;
        result.nodes.push_back(n);
    }
    // Sort nodes by id for readability
    std::sort(result.nodes.begin(), result.nodes.end(),
        [](const GraphNode& a, const GraphNode& b){ return a.id < b.id; });

    // Build edges (directed, one per observed adjacency)
    std::set<std::pair<int,int>> edgeSet;
    for (auto& [pos, obj] : grid) {
        for (Dir d : allDirs) {
            glm::ivec2 nbPos = { pos.first + dirVec(d).x,
                                 pos.second + dirVec(d).y };
            auto it = grid.find({nbPos.x, nbPos.y});
            if (it == grid.end()) continue;
            auto& nb = it->second;
            // Store only one direction per pair (lower id → higher id)
            auto epair = std::make_pair(std::min(obj.id, nb.id),
                                        std::max(obj.id, nb.id));
            if (!edgeSet.count(epair)) {
                edgeSet.insert(epair);
                GraphEdge e;
                e.fromId = obj.id;
                e.toId   = nb.id;
                e.dir    = d;
                result.edges.push_back(e);
            }
        }
    }
    // Sort edges
    std::sort(result.edges.begin(), result.edges.end(),
        [](const GraphEdge& a, const GraphEdge& b){
            if (a.fromId != b.fromId) return a.fromId < b.fromId;
            return a.toId < b.toId;
        });

    std::cout << "[GrammarInducer] Induced grammar from "
              << grid.size()         << " tiles: "
              << result.tileVariants.size() << " variants, "
              << result.rules.size()        << " rules, "
              << result.edges.size()        << " graph edges\n";

    return result;
}

} // namespace grammar

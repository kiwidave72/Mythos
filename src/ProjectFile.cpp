#include "ProjectFile.h"
#include "ObjImporter.h"
#include "GltfImporter.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

std::string ProjectFile::s_error;

// ============================================================
// JSON write helpers
// ============================================================

std::string ProjectFile::escapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

std::string ProjectFile::vec3Json(const glm::vec3& v)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "[%.6f,%.6f,%.6f]", v.x, v.y, v.z);
    return buf;
}

std::string ProjectFile::vec2iJson(const glm::ivec2& v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "[%d,%d]", v.x, v.y);
    return buf;
}

// ============================================================
// Save
// ============================================================

bool ProjectFile::save(const std::string& path,
                       const Camera&      camera,
                       const GrammarView& grammar,
                       const Scene&       scene)
{
    std::ofstream f(path);
    if (!f) {
        s_error = "Cannot open for writing: " + path;
        std::cerr << "[ProjectFile] " << s_error << "\n";
        return false;
    }

    const auto& gs = grammar.settings();

    f << "{\n";

    // ---- Camera ----
    f << "  \"camera\": {\n";
    f << "    \"target\": " << vec3Json(camera.target) << ",\n";
    f << "    \"yaw\": "    << camera.yaw   << ",\n";
    f << "    \"pitch\": "  << camera.pitch << ",\n";
    f << "    \"dist\": "   << camera.dist  << "\n";
    f << "  },\n";

    // ---- Grammar settings ----
    f << "  \"grammar\": {\n";
    f << "    \"seed\": "       << gs.seed       << ",\n";
    f << "    \"minPrim\": "    << gs.minPrim    << ",\n";
    f << "    \"maxPrim\": "    << gs.maxPrim    << ",\n";
    f << "    \"hardcoded\": "  << (gs.hardcoded ? "true" : "false") << "\n";
    f << "  },\n";

    // ---- Scene objects ----
    f << "  \"objects\": [\n";
    const auto& objs = scene.objects();
    for (size_t i = 0; i < objs.size(); ++i) {
        const SceneObject& o = objs[i];
        f << "    {\n";
        f << "      \"id\": "       << o.id                          << ",\n";
        f << "      \"name\": \""   << escapeJson(o.name)            << "\",\n";
        f << "      \"primId\": \"" << escapeJson(o.primId)          << "\",\n";
        f << "      \"position\": " << vec3Json(o.position)          << ",\n";
        f << "      \"rotation\": " << vec3Json(o.rotation)          << ",\n";
        f << "      \"scale\": "    << vec3Json(o.scale)             << ",\n";
        f << "      \"color\": "    << vec3Json(o.color)             << ",\n";
        f << "      \"gridCell\": " << vec2iJson(o.gridCell)         << ",\n";
        f << "      \"visible\": "  << (o.visible ? "true" : "false") << ",\n";

        // Mesh source — empty string for procedural cubes
        std::string src = (o.mesh && !o.mesh->sourcePath.empty())
                        ? o.mesh->sourcePath : "";
        f << "      \"meshSource\": \"" << escapeJson(src) << "\",\n";

        // Mesh name (used to match procedural cubes in MeshLibrary)
        std::string mname = o.mesh ? o.mesh->name : "";
        f << "      \"meshName\": \"" << escapeJson(mname) << "\",\n";

        // Mesh color — needed to recreate procedural cubes on load
        f << "      \"meshColor\": " << vec3Json(o.color) << ",\n";

        // Sockets
        f << "      \"sockets\": [\n";
        for (size_t si = 0; si < o.sockets.size(); ++si) {
            const WorldSocket& ws = o.sockets[si];
            f << "        {\n";
            f << "          \"worldPos\": "  << vec3Json(ws.worldPos)  << ",\n";
            f << "          \"worldNorm\": " << vec3Json(ws.worldNorm) << ",\n";
            f << "          \"gridDir\": "   << vec2iJson(ws.gridDir)  << ",\n";
            f << "          \"connected\": " << (ws.connected ? "true" : "false") << ",\n";
            f << "          \"connectedTo\": " << ws.connectedTo << "\n";
            f << "        }" << (si+1 < o.sockets.size() ? "," : "") << "\n";
        }
        f << "      ]\n";
        f << "    }" << (i+1 < objs.size() ? "," : "") << "\n";
    }
    f << "  ]\n";
    f << "}\n";

    std::cout << "[ProjectFile] Saved " << objs.size()
              << " objects to: " << path << "\n";
    s_error.clear();
    return true;
}

// ============================================================
// Minimal JSON parser (reused from GltfImporter style)
// ============================================================

struct JV;
using JA = std::vector<JV>;
using JO = std::vector<std::pair<std::string,JV>>;

struct JV {
    enum { Null,Bool,Num,Str,Arr,Obj } type = Null;
    bool b=false; double n=0; std::string s;
    std::shared_ptr<JA> arr; std::shared_ptr<JO> obj;

    double      num()  const { return type==Num?n:0; }
    int         inum() const { return (int)num(); }
    std::string str()  const { return type==Str?s:""; }
    bool        boolean() const { return type==Bool?b:false; }

    const JV& operator[](const std::string& k) const {
        static JV null;
        if (type!=Obj) return null;
        for (auto& kv:*obj) if(kv.first==k) return kv.second;
        return null;
    }
    const JV& operator[](size_t i) const {
        static JV null;
        if (type!=Arr||i>=arr->size()) return null;
        return (*arr)[i];
    }
    size_t size() const {
        if(type==Arr) return arr->size();
        if(type==Obj) return obj->size();
        return 0;
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
        if(c=='t'){p+=4;JV v;v.type=JV::Bool;v.b=true;return v;}
        if(c=='f'){p+=5;JV v;v.type=JV::Bool;v.b=false;return v;}
        if(c=='n'){p+=4;return {};}
        return pNum();
    }
    JV pObj(){
        ++p; JV v; v.type=JV::Obj; v.obj=std::make_shared<JO>();
        ws(); while(p<end&&*p!='}'){
            if(*p==','){++p;ws();continue;}
            JV k=pStr(); ws(); if(p<end&&*p==':')++p;
            JV val=parse(); v.obj->push_back({k.s,val}); ws();
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
            if(*p=='\\'){++p;if(p<end)v.s+=*p++;}
            else v.s+=*p++;
        }
        if(p<end)++p; return v;
    }
    JV pNum(){
        const char* s=p;
        if(*p=='-')++p;
        while(p<end&&(std::isdigit((unsigned char)*p)||*p=='.'||*p=='e'||*p=='E'||*p=='+'||*p=='-'))++p;
        JV v; v.type=JV::Num; v.n=std::stod(std::string(s,p)); return v;
    }
};

static glm::vec3 readVec3(const JV& arr)
{
    return { (float)arr[0].num(), (float)arr[1].num(), (float)arr[2].num() };
}
static glm::ivec2 readVec2i(const JV& arr)
{
    return { arr[0].inum(), arr[1].inum() };
}

// Route to correct importer
static std::shared_ptr<MeshAsset> importMeshFile(const std::string& path)
{
    std::string ext;
    if (path.size() >= 4) ext = path.substr(path.size()-4);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    std::string ext5;
    if (path.size() >= 5) ext5 = path.substr(path.size()-5);
    for (auto& c : ext5) c = (char)tolower((unsigned char)c);

    if (ext == ".glb" || ext5 == ".gltf")
        return GltfImporter::load(path);
    return ObjImporter::load(path);
}

// ============================================================
// Load
// ============================================================

bool ProjectFile::load(const std::string& path,
                       Camera&      camera,
                       GrammarView& grammar,
                       Scene&       scene,
                       MeshLibrary& meshLib)
{
    std::ifstream f(path);
    if (!f) {
        s_error = "Cannot open: " + path;
        std::cerr << "[ProjectFile] " << s_error << "\n";
        return false;
    }
    std::string json((std::istreambuf_iterator<char>(f)), {});

    JP parser(json.data(), json.size());
    JV root = parser.parse();
    if (root.type != JV::Obj) {
        s_error = "Invalid JSON in: " + path;
        std::cerr << "[ProjectFile] " << s_error << "\n";
        return false;
    }

    // ---- Camera ----
    const JV& cam = root["camera"];
    if (cam.type == JV::Obj) {
        camera.target = readVec3(cam["target"]);
        camera.yaw    = (float)cam["yaw"].num();
        camera.pitch  = (float)cam["pitch"].num();
        camera.dist   = (float)cam["dist"].num();
    }

    // ---- Grammar settings ----
    const JV& gs = root["grammar"];
    if (gs.type == JV::Obj) {
        GrammarView::Settings s;
        s.seed      = gs["seed"].inum();
        s.minPrim   = gs["minPrim"].inum();
        s.maxPrim   = gs["maxPrim"].inum();
        s.hardcoded = gs["hardcoded"].boolean();
        grammar.applySettings(s);
    }

    // ---- Scene objects ----
    // Stop any in-progress grammar generation — we're restoring a saved scene,
    // not running the generator. applySettings() above does NOT trigger generation.
    grammar.stopGenerating();
    scene.clear();

    // Cache of already-loaded mesh assets so we don't re-import duplicates
    std::map<std::string, std::shared_ptr<MeshAsset>> meshCache;

    const JV& objs = root["objects"];
    int loaded  = 0;
    int maxId   = 0;

    for (size_t i = 0; i < objs.size(); ++i) {
        const JV& jo = objs[i];

        SceneObject& o = scene.addObject();
        o.name     = jo["name"].str();
        o.primId   = jo["primId"].str();
        o.position = readVec3(jo["position"]);
        o.rotation = readVec3(jo["rotation"]);
        o.scale    = readVec3(jo["scale"]);
        o.color    = readVec3(jo["color"]);
        o.gridCell = readVec2i(jo["gridCell"]);
        o.visible  = jo["visible"].boolean();

        // Track highest ID so m_nextId stays correct after load
        if (o.id > maxId) maxId = o.id;

        // Restore mesh
        std::string meshSrc   = jo["meshSource"].str();
        std::string meshName  = jo["meshName"].str();
        glm::vec3   meshColor = jo["meshColor"].size() >= 3
                              ? readVec3(jo["meshColor"])
                              : o.color;

        if (!meshSrc.empty()) {
            // Imported file mesh — re-load from disk, cache to avoid duplicates
            auto it = meshCache.find(meshSrc);
            if (it != meshCache.end()) {
                o.mesh = it->second;
            } else {
                auto asset = importMeshFile(meshSrc);
                if (asset && asset->upload()) {
                    meshCache[meshSrc] = asset;
                    o.mesh = asset;
                } else {
                    std::cerr << "[ProjectFile] Could not reload mesh: "
                              << meshSrc << "\n";
                }
            }
        } else if (!meshName.empty()) {
            // Procedural cube — getOrCreateCube registers+uploads it if absent.
            // Extract the primId from the mesh name (format "cube:<primId>")
            std::string primId = meshName;
            if (primId.substr(0,5) == "cube:") primId = primId.substr(5);
            o.mesh = meshLib.getOrCreateCube(primId, meshColor);
            if (o.mesh && !o.mesh->isLoaded()) o.mesh->upload();
        }

        // Restore sockets
        const JV& socks = jo["sockets"];
        for (size_t si = 0; si < socks.size(); ++si) {
            const JV& js = socks[si];
            WorldSocket ws;
            ws.worldPos    = readVec3(js["worldPos"]);
            ws.worldNorm   = readVec3(js["worldNorm"]);
            ws.gridDir     = readVec2i(js["gridDir"]);
            ws.connected   = js["connected"].boolean();
            ws.connectedTo = js["connectedTo"].inum();
            o.sockets.push_back(ws);
        }

        ++loaded;
    }

    // Ensure IDs issued after load don't collide with restored IDs
    scene.setNextId(maxId + 1);

    // Rebuild the grid cell → object ID lookup map
    scene.rebuildCellMap();

    std::cout << "[ProjectFile] Loaded " << loaded
              << " objects from: " << path << "\n";
    s_error.clear();
    return true;
}

#include "GltfImporter.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <algorithm>

// ============================================================
// Minimal JSON value — enough to parse glTF
// ============================================================

struct JVal;
using JArr = std::vector<JVal>;
using JObj = std::vector<std::pair<std::string, JVal>>;

struct JVal {
    enum Type { Null, Bool, Num, Str, Array, Object } type = Null;
    bool        b   = false;
    double      n   = 0;
    std::string s;
    std::shared_ptr<JArr> arr;
    std::shared_ptr<JObj> obj;

    bool isNull()   const { return type == Null; }
    bool isNum()    const { return type == Num; }
    bool isStr()    const { return type == Str; }
    bool isArr()    const { return type == Array; }
    bool isObj()    const { return type == Object; }

    double      num()    const { return isNum() ? n : 0; }
    int         inum()   const { return (int)num(); }
    std::string str()    const { return isStr() ? s : ""; }

    const JVal& operator[](const std::string& key) const {
        static JVal null;
        if (!isObj()) return null;
        for (auto& kv : *obj) if (kv.first == key) return kv.second;
        return null;
    }
    const JVal& operator[](size_t i) const {
        static JVal null;
        if (!isArr() || i >= arr->size()) return null;
        return (*arr)[i];
    }
    size_t size() const {
        if (isArr()) return arr->size();
        if (isObj()) return obj->size();
        return 0;
    }
    bool has(const std::string& k) const {
        if (!isObj()) return false;
        for (auto& kv : *obj) if (kv.first == k) return true;
        return false;
    }
};

// ---- Parser ----------------------------------------------------------------

struct Parser {
    const char* p;
    const char* end;

    Parser(const char* data, size_t len) : p(data), end(data+len) {}

    void skipWS() { while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) ++p; }

    JVal parse() {
        skipWS();
        if (p >= end) return {};
        char c = *p;
        if (c == '{') return parseObj();
        if (c == '[') return parseArr();
        if (c == '"') return parseStr();
        if (c == 't') { p+=4; JVal v; v.type=JVal::Bool; v.b=true;  return v; }
        if (c == 'f') { p+=5; JVal v; v.type=JVal::Bool; v.b=false; return v; }
        if (c == 'n') { p+=4; return {}; }
        return parseNum();
    }

    JVal parseObj() {
        ++p; // '{'
        JVal v; v.type=JVal::Object; v.obj=std::make_shared<JObj>();
        skipWS();
        while (p < end && *p != '}') {
            if (*p == ',') { ++p; skipWS(); continue; }
            JVal key = parseStr();
            skipWS(); if (*p==':') ++p;
            JVal val = parse();
            v.obj->push_back({key.s, val});
            skipWS();
        }
        if (p < end) ++p;
        return v;
    }

    JVal parseArr() {
        ++p; // '['
        JVal v; v.type=JVal::Array; v.arr=std::make_shared<JArr>();
        skipWS();
        while (p < end && *p != ']') {
            if (*p == ',') { ++p; skipWS(); continue; }
            v.arr->push_back(parse());
            skipWS();
        }
        if (p < end) ++p;
        return v;
    }

    JVal parseStr() {
        if (*p == '"') ++p;
        JVal v; v.type=JVal::Str;
        while (p < end && *p != '"') {
            if (*p == '\\') { ++p; if (p<end) v.s += *p++; }
            else v.s += *p++;
        }
        if (p < end) ++p;
        return v;
    }

    JVal parseNum() {
        const char* start = p;
        if (*p=='-') ++p;
        while (p<end && (std::isdigit((unsigned char)*p)||*p=='.'||*p=='e'||*p=='E'||*p=='+'||*p=='-')) ++p;
        JVal v; v.type=JVal::Num; v.n=std::stod(std::string(start,p));
        return v;
    }
};

// ============================================================
// GLB helpers
// ============================================================

struct GlbChunk { uint32_t length; uint32_t type; const uint8_t* data; };

static bool readGlbChunks(const std::vector<uint8_t>& raw,
                          std::string& jsonOut,
                          std::vector<uint8_t>& binOut)
{
    if (raw.size() < 12) return false;
    uint32_t magic, version, totalLen;
    memcpy(&magic,    raw.data(),   4);
    memcpy(&version,  raw.data()+4, 4);
    memcpy(&totalLen, raw.data()+8, 4);
    if (magic != 0x46546C67u) return false; // "glTF"

    size_t off = 12;
    while (off + 8 <= raw.size()) {
        uint32_t cLen, cType;
        memcpy(&cLen,  raw.data()+off,   4);
        memcpy(&cType, raw.data()+off+4, 4);
        off += 8;
        if (off + cLen > raw.size()) break;
        if (cType == 0x4E4F534Au) // JSON
            jsonOut.assign(raw.data()+off, raw.data()+off+cLen);
        else if (cType == 0x004E4942u) // BIN
            binOut.assign(raw.data()+off, raw.data()+off+cLen);
        off += cLen;
    }
    return !jsonOut.empty();
}

// ============================================================
// Accessor reading
// ============================================================

// componentType → byte size
static int compSize(int ct) {
    switch(ct) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: return 4;
    }
}
static int typeCount(const std::string& t) {
    if (t=="SCALAR") return 1;
    if (t=="VEC2")   return 2;
    if (t=="VEC3")   return 3;
    if (t=="VEC4")   return 4;
    if (t=="MAT4")   return 16;
    return 1;
}

struct BufView {
    const uint8_t* data;
    size_t byteOffset;
    size_t byteLength;
    size_t byteStride;
};

// Read a float from raw bytes based on componentType
static float readFloat(const uint8_t* ptr, int componentType)
{
    switch (componentType) {
        case 5126: { float f; memcpy(&f,ptr,4); return f; }
        case 5123: { uint16_t u; memcpy(&u,ptr,2); return u; }
        case 5122: { int16_t  s; memcpy(&s,ptr,2); return s; }
        case 5121: return *ptr;
        case 5120: return (int8_t)*ptr;
        default:   { float f; memcpy(&f,ptr,4); return f; }
    }
}

static uint32_t readIndex(const uint8_t* ptr, int componentType)
{
    switch (componentType) {
        case 5125: { uint32_t u; memcpy(&u,ptr,4); return u; }
        case 5123: { uint16_t u; memcpy(&u,ptr,2); return u; }
        case 5121: return *ptr;
        default:   { uint32_t u; memcpy(&u,ptr,4); return u; }
    }
}

// ============================================================
// Main loader
// ============================================================

std::shared_ptr<MeshAsset> GltfImporter::load(const std::string& path)
{
    bool isGlb = path.size()>=4 && path.substr(path.size()-4)==".glb";

    std::string jsonStr;
    std::vector<uint8_t> embeddedBin;

    if (isGlb) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { std::cerr<<"[GltfImporter] Cannot open: "<<path<<"\n"; return nullptr; }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
        if (!readGlbChunks(raw, jsonStr, embeddedBin)) {
            std::cerr<<"[GltfImporter] Not a valid GLB: "<<path<<"\n"; return nullptr;
        }
    } else {
        std::ifstream f(path);
        if (!f) { std::cerr<<"[GltfImporter] Cannot open: "<<path<<"\n"; return nullptr; }
        jsonStr.assign((std::istreambuf_iterator<char>(f)), {});
    }

    // Parse JSON
    Parser jparser(jsonStr.data(), jsonStr.size());
    JVal root = jparser.parse();
    if (!root.isObj()) { std::cerr<<"[GltfImporter] Invalid JSON in: "<<path<<"\n"; return nullptr; }

    // Resolve directory for external buffer loading
    std::string dir;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) dir = path.substr(0, slash+1);

    // Load buffers
    const JVal& buffers = root["buffers"];
    std::vector<std::vector<uint8_t>> bufs;
    for (size_t i = 0; i < buffers.size(); ++i) {
        const JVal& buf = buffers[i];
        if (isGlb && i==0 && !embeddedBin.empty()) {
            bufs.push_back(embeddedBin);
        } else {
            std::string uri = buf["uri"].str();
            // data URI: data:application/octet-stream;base64,...
            if (uri.substr(0,5) == "data:") {
                size_t comma = uri.find(',');
                if (comma != std::string::npos) {
                    // Base64 decode
                    static const std::string B64 =
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                    std::string b64 = uri.substr(comma+1);
                    std::vector<uint8_t> decoded;
                    decoded.reserve(b64.size()*3/4);
                    int val=0, bits=-8;
                    for (unsigned char c : b64) {
                        size_t pos = B64.find(c);
                        if (pos==std::string::npos) continue;
                        val = (val<<6)|(int)pos; bits+=6;
                        if (bits>=0) { decoded.push_back((val>>bits)&0xFF); bits-=8; }
                    }
                    bufs.push_back(decoded);
                } else { bufs.emplace_back(); }
            } else {
                std::ifstream bf(dir+uri, std::ios::binary);
                std::vector<uint8_t> bdata((std::istreambuf_iterator<char>(bf)), {});
                bufs.push_back(bdata);
            }
        }
    }

    // Build buffer views
    const JVal& bvArr = root["bufferViews"];
    std::vector<BufView> bviews;
    for (size_t i=0; i<bvArr.size(); ++i) {
        const JVal& bv = bvArr[i];
        BufView view{};
        int bufIdx = bv["buffer"].inum();
        view.byteOffset = (size_t)bv["byteOffset"].num();
        view.byteLength = (size_t)bv["byteLength"].num();
        view.byteStride = bv.has("byteStride") ? (size_t)bv["byteStride"].num() : 0;
        view.data = (bufIdx>=0 && bufIdx<(int)bufs.size()) ? bufs[bufIdx].data() : nullptr;
        bviews.push_back(view);
    }

    // Build accessors metadata
    const JVal& accArr = root["accessors"];
    struct AccMeta { int bvIdx; size_t byteOffset; int compType; int count; std::string type; size_t stride; };
    std::vector<AccMeta> accs;
    for (size_t i=0; i<accArr.size(); ++i) {
        const JVal& a = accArr[i];
        AccMeta m{};
        m.bvIdx      = a.has("bufferView") ? a["bufferView"].inum() : -1;
        m.byteOffset = (size_t)a["byteOffset"].num();
        m.compType   = a["componentType"].inum();
        m.count      = a["count"].inum();
        m.type       = a["type"].str();
        if (m.bvIdx>=0 && m.bvIdx<(int)bviews.size())
            m.stride = bviews[m.bvIdx].byteStride
                     ? bviews[m.bvIdx].byteStride
                     : (size_t)(typeCount(m.type)*compSize(m.compType));
        accs.push_back(m);
    }

    // Helper: get raw pointer to element i of accessor
    auto accPtr = [&](int accIdx, int elemIdx) -> const uint8_t* {
        if (accIdx<0 || accIdx>=(int)accs.size()) return nullptr;
        auto& m = accs[accIdx];
        if (m.bvIdx<0 || m.bvIdx>=(int)bviews.size()) return nullptr;
        auto& bv = bviews[m.bvIdx];
        if (!bv.data) return nullptr;
        return bv.data + bv.byteOffset + m.byteOffset + (size_t)elemIdx*m.stride;
    };

    // Parse materials → baseColorFactor
    const JVal& matArr = root["materials"];
    std::vector<glm::vec3> matColors;
    for (size_t i=0; i<matArr.size(); ++i) {
        glm::vec3 col{0.75f,0.75f,0.75f};
        const JVal& pbr = matArr[i]["pbrMetallicRoughness"];
        if (!pbr.isNull() && pbr.has("baseColorFactor")) {
            const JVal& bc = pbr["baseColorFactor"];
            if (bc.size()>=3)
                col = {(float)bc[0].num(), (float)bc[1].num(), (float)bc[2].num()};
        }
        matColors.push_back(col);
    }

    // Build mesh asset
    auto asset = std::make_shared<MeshAsset>();
    asset->sourcePath = path;
    size_t slashP = path.find_last_of("/\\");
    size_t dotP   = path.rfind('.');
    asset->name = "gltf:" + path.substr(
        slashP==std::string::npos?0:slashP+1,
        dotP==std::string::npos?std::string::npos:dotP-(slashP==std::string::npos?0:slashP+1));

    const JVal& meshArr = root["meshes"];
    bool hasMaterials = matArr.size() > 0;

    for (size_t mi=0; mi<meshArr.size(); ++mi) {
        const JVal& mesh = meshArr[mi];
        const JVal& prims = mesh["primitives"];

        for (size_t pi=0; pi<prims.size(); ++pi) {
            const JVal& prim = prims[pi];
            const JVal& attrs = prim["attributes"];

            int posAcc  = attrs.has("POSITION") ? attrs["POSITION"].inum() : -1;
            int normAcc = attrs.has("NORMAL")   ? attrs["NORMAL"].inum()   : -1;
            int uvAcc   = attrs.has("TEXCOORD_0")? attrs["TEXCOORD_0"].inum(): -1;
            int idxAcc  = prim.has("indices")   ? prim["indices"].inum()   : -1;
            int matIdx  = prim.has("material")  ? prim["material"].inum()  : -1;

            if (posAcc < 0) continue;

            int vertCount = accs[posAcc].count;
            int baseVert  = (int)asset->data.vertices.size();
            int baseIdx   = (int)asset->data.indices.size();

            // Read vertices
            for (int vi=0; vi<vertCount; ++vi) {
                MeshVertex mv{};
                const uint8_t* pp = accPtr(posAcc, vi);
                if (pp) { memcpy(&mv.pos.x,pp,4); memcpy(&mv.pos.y,pp+4,4); memcpy(&mv.pos.z,pp+8,4); }

                if (normAcc>=0) {
                    const uint8_t* np = accPtr(normAcc, vi);
                    if (np) { memcpy(&mv.normal.x,np,4); memcpy(&mv.normal.y,np+4,4); memcpy(&mv.normal.z,np+8,4); }
                }
                if (uvAcc>=0) {
                    const uint8_t* tp = accPtr(uvAcc, vi);
                    if (tp) {
                        float u,v2; memcpy(&u,tp,4); memcpy(&v2,tp+4,4);
                        mv.uv = {u, v2};
                    }
                }
                asset->data.vertices.push_back(mv);
            }

            // Read indices
            if (idxAcc>=0) {
                int idxCount = accs[idxAcc].count;
                for (int ii=0; ii<idxCount; ++ii) {
                    const uint8_t* ip = accPtr(idxAcc, ii);
                    if (ip) asset->data.indices.push_back(
                        baseVert + readIndex(ip, accs[idxAcc].compType));
                }
            } else {
                // Non-indexed: generate sequential
                for (int vi=0; vi<vertCount; ++vi)
                    asset->data.indices.push_back(baseVert + vi);
            }

            // Build flat normals if missing
            if (normAcc < 0) {
                // We'll fix up after all prims are added
            }

            // Submesh entry
            if (hasMaterials) {
                SubMesh sm;
                sm.materialName = mesh["name"].str();
                if (sm.materialName.empty())
                    sm.materialName = "prim_" + std::to_string(pi);
                sm.indexOffset  = baseIdx * (int)sizeof(unsigned int);
                sm.indexCount   = (int)asset->data.indices.size() - baseIdx;
                sm.color = (matIdx>=0 && matIdx<(int)matColors.size())
                           ? matColors[matIdx]
                           : glm::vec3{0.75f,0.75f,0.75f};
                asset->submeshes.push_back(sm);
            }
        }
    }

    if (asset->data.vertices.empty()) {
        std::cerr<<"[GltfImporter] No geometry found in: "<<path<<"\n";
        return nullptr;
    }

    // Compute flat normals for any verts that ended up with zero normals
    // (i.e. the source had no NORMAL attribute)
    bool needsFlatNormals = false;
    for (auto& v : asset->data.vertices)
        if (glm::length(v.normal) < 0.01f) { needsFlatNormals = true; break; }
    if (needsFlatNormals)
        computeFlatNormals(asset->data);

    asset->data.computeAABB();

    std::cout<<"[GltfImporter] Loaded '"<<path<<"': "
             <<asset->data.vertices.size()<<" verts, "
             <<asset->data.indices.size()/3<<" tris";
    if (!asset->submeshes.empty())
        std::cout<<", "<<asset->submeshes.size()<<" material groups";
    std::cout<<"\n";

    return asset;
}

// ============================================================
// Flat normals
// ============================================================
void GltfImporter::computeFlatNormals(MeshData& data)
{
    // First pass: accumulate face normals onto vertices
    for (size_t i=0; i+2<data.indices.size(); i+=3) {
        auto& v0 = data.vertices[data.indices[i]];
        auto& v1 = data.vertices[data.indices[i+1]];
        auto& v2 = data.vertices[data.indices[i+2]];
        glm::vec3 n = glm::normalize(glm::cross(v1.pos-v0.pos, v2.pos-v0.pos));
        v0.normal += n; v1.normal += n; v2.normal += n;
    }
    for (auto& v : data.vertices)
        if (glm::length(v.normal) > 0.001f)
            v.normal = glm::normalize(v.normal);
}

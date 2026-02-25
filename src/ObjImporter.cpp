#include "ObjImporter.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <array>

// ============================================================
// Internal helpers
// ============================================================

struct ObjIndex {
    int v = 0, vt = 0, vn = 0;  // 1-based, 0 = absent
    bool operator==(const ObjIndex& o) const {
        return v == o.v && vt == o.vt && vn == o.vn;
    }
};

struct ObjIndexHash {
    size_t operator()(const ObjIndex& i) const {
        size_t h = std::hash<int>{}(i.v);
        h ^= std::hash<int>{}(i.vt) + 0x9e3779b9 + (h<<6) + (h>>2);
        h ^= std::hash<int>{}(i.vn) + 0x9e3779b9 + (h<<6) + (h>>2);
        return h;
    }
};

// Parse "v/vt/vn" or "v//vn" or "v/vt" or "v" into ObjIndex
static ObjIndex parseFaceVert(const std::string& tok)
{
    ObjIndex idx;
    std::istringstream ss(tok);
    std::string part;
    int field = 0;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            int val = std::stoi(part);
            if (field == 0) idx.v  = val;
            if (field == 1) idx.vt = val;
            if (field == 2) idx.vn = val;
        }
        ++field;
    }
    return idx;
}

// ============================================================
// ObjImporter::load
// ============================================================

std::shared_ptr<MeshAsset> ObjImporter::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[ObjImporter] Cannot open: " << path << "\n";
        return nullptr;
    }

    // Raw OBJ data (1-based indexing, size+1 to keep 1-based access clean)
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    positions.push_back({});  // index 0 unused
    normals.push_back({});
    uvs.push_back({});

    // Face index triples before deduplication
    std::vector<ObjIndex>  faceVerts;

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (token == "vn") {
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(glm::normalize(n));
        }
        else if (token == "vt") {
            glm::vec2 t;
            ss >> t.x >> t.y;
            uvs.push_back(t);
        }
        else if (token == "f") {
            // Read face verts — may be triangle or quad or n-gon
            std::vector<ObjIndex> face;
            std::string vtok;
            while (ss >> vtok)
                face.push_back(parseFaceVert(vtok));

            if (face.size() < 3) {
                std::cerr << "[ObjImporter] Line " << lineNum
                          << ": degenerate face, skipping\n";
                continue;
            }

            // Fan-triangulate n-gons: (0,1,2), (0,2,3), (0,3,4), ...
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                faceVerts.push_back(face[0]);
                faceVerts.push_back(face[i]);
                faceVerts.push_back(face[i+1]);
            }
        }
        // Silently ignore: mtllib, usemtl, o, g, s, l
    }

    if (faceVerts.empty()) {
        std::cerr << "[ObjImporter] No faces found in: " << path << "\n";
        return nullptr;
    }

    const bool hasNormals = normals.size() > 1;
    const bool hasUVs     = uvs.size() > 1;

    // ---- Deduplicate into indexed mesh ----
    std::unordered_map<ObjIndex, unsigned int, ObjIndexHash> seen;
    auto asset = std::make_shared<MeshAsset>();
    asset->sourcePath = path;

    // Derive name from filename
    size_t slash = path.find_last_of("/\\");
    size_t dot   = path.rfind('.');
    asset->name  = "obj:" + path.substr(
        slash == std::string::npos ? 0 : slash+1,
        dot   == std::string::npos ? std::string::npos : dot - (slash == std::string::npos ? 0 : slash+1));

    for (auto& fi : faceVerts) {
        auto it = seen.find(fi);
        if (it != seen.end()) {
            asset->data.indices.push_back(it->second);
        } else {
            MeshVertex mv{};

            // Clamp to valid range (handle malformed OBJ gracefully)
            if (fi.v > 0 && fi.v < (int)positions.size())
                mv.pos = positions[fi.v];

            if (hasNormals && fi.vn > 0 && fi.vn < (int)normals.size())
                mv.normal = normals[fi.vn];

            if (hasUVs && fi.vt > 0 && fi.vt < (int)uvs.size())
                mv.uv = uvs[fi.vt];

            unsigned int newIdx = (unsigned int)asset->data.vertices.size();
            asset->data.vertices.push_back(mv);
            asset->data.indices.push_back(newIdx);
            seen[fi] = newIdx;
        }
    }

    // If the OBJ had no normals, compute flat normals from triangles
    if (!hasNormals)
        computeFlatNormals(asset->data);

    asset->data.computeAABB();

    std::cout << "[ObjImporter] Loaded '" << path << "': "
              << asset->data.vertices.size() << " verts, "
              << asset->data.indices.size()/3 << " tris"
              << (hasNormals ? "" : " [flat normals]")
              << "\n";

    return asset;
}

// ============================================================
// Flat normal generation (per-triangle)
// ============================================================

void ObjImporter::computeFlatNormals(MeshData& data)
{
    // Each triangle gets a normal from its cross product.
    // We need to duplicate vertices so each triangle has its own normal.
    std::vector<MeshVertex>   newVerts;
    std::vector<unsigned int> newIdx;
    newVerts.reserve(data.indices.size());
    newIdx.reserve(data.indices.size());

    for (size_t i = 0; i < data.indices.size(); i += 3) {
        auto& v0 = data.vertices[data.indices[i+0]];
        auto& v1 = data.vertices[data.indices[i+1]];
        auto& v2 = data.vertices[data.indices[i+2]];

        glm::vec3 n = glm::normalize(
            glm::cross(v1.pos - v0.pos, v2.pos - v0.pos));

        for (int j = 0; j < 3; ++j) {
            MeshVertex mv = data.vertices[data.indices[i+j]];
            mv.normal = n;
            unsigned int ni = (unsigned int)newVerts.size();
            newVerts.push_back(mv);
            newIdx.push_back(ni);
        }
    }

    data.vertices = std::move(newVerts);
    data.indices  = std::move(newIdx);
}

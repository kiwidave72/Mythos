#include "AssetLibrary.h"
#include "ObjImporter.h"
#include "GltfImporter.h"
#include <algorithm>

// Route to correct importer based on file extension
static std::shared_ptr<MeshAsset> importMesh(const std::string& path)
{
    std::string ext;
    if (path.size() >= 4) ext = path.substr(path.size() - 4);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);

    std::string ext5;
    if (path.size() >= 5) ext5 = path.substr(path.size() - 5);
    for (auto& c : ext5) c = (char)tolower((unsigned char)c);

    if (ext == ".glb" || ext5 == ".gltf")
        return GltfImporter::load(path);
    return ObjImporter::load(path);
}
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// ============================================================
// AssetEntry
// ============================================================

glm::mat4 AssetEntry::calibMatrix() const
{
    glm::mat4 m = glm::mat4(1.f);
    m = glm::translate(m, calibPos);
    m = glm::rotate(m, glm::radians(calibRot.y), {0,1,0});
    m = glm::rotate(m, glm::radians(calibRot.x), {1,0,0});
    m = glm::rotate(m, glm::radians(calibRot.z), {0,0,1});
    m = glm::scale(m, calibScale);
    return m;
}

// ============================================================
// Minimal JSON helpers (no external dependency)
// ============================================================

// Escape a string for JSON output
static std::string jsonStr(const std::string& s)
{
    std::string r = "\"";
    for (char c : s) {
        if (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    r += "\"";
    return r;
}

static std::string jsonF(float f)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", f);
    return buf;
}

// Minimal JSON reader helpers
static std::string jsonGetString(const std::string& json,
                                 const std::string& key)
{
    std::string search = "\"" + key + "\"";
    size_t k = json.find(search);
    if (k == std::string::npos) return "";
    size_t q1 = json.find('"', k + search.size() + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static float jsonGetFloat(const std::string& json,
                          const std::string& key, float def = 0.f)
{
    std::string search = "\"" + key + "\"";
    size_t k = json.find(search);
    if (k == std::string::npos) return def;
    size_t col = json.find(':', k + search.size());
    if (col == std::string::npos) return def;
    try { return std::stof(json.substr(col + 1)); }
    catch (...) { return def; }
}

static glm::vec3 jsonGetVec3(const std::string& block,
                              const std::string& key)
{
    // Reads "[x, y, z]" array after "key":
    std::string search = "\"" + key + "\"";
    size_t k = block.find(search);
    if (k == std::string::npos) return {0,0,0};
    size_t lb = block.find('[', k);
    size_t rb = block.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) return {0,0,0};
    std::string arr = block.substr(lb+1, rb-lb-1);
    // parse 3 comma-separated floats
    glm::vec3 v{0,0,0};
    std::istringstream ss(arr);
    std::string tok;
    int i = 0;
    while (std::getline(ss, tok, ',') && i < 3) {
        try { v[i++] = std::stof(tok); } catch (...) {}
    }
    return v;
}

static std::string vec3ToJson(const glm::vec3& v)
{
    return "[" + jsonF(v.x) + "," + jsonF(v.y) + "," + jsonF(v.z) + "]";
}

// ============================================================
// AssetLibrary::save
// ============================================================

void AssetLibrary::save(const std::string& path) const
{
    std::ofstream f(path);
    if (!f) {
        std::cerr << "[AssetLibrary] Cannot write: " << path << "\n";
        return;
    }

    f << "{\n  \"assets\": [\n";
    for (int i = 0; i < (int)m_entries.size(); ++i) {
        const auto& e = m_entries[i];
        f << "    {\n";
        f << "      \"name\": "       << jsonStr(e.name)       << ",\n";
        f << "      \"sourcePath\": " << jsonStr(e.sourcePath) << ",\n";
        f << "      \"calibPos\": "   << vec3ToJson(e.calibPos)   << ",\n";
        f << "      \"calibRot\": "   << vec3ToJson(e.calibRot)   << ",\n";
        f << "      \"calibScale\": " << vec3ToJson(e.calibScale) << "\n";
        f << "    }";
        if (i + 1 < (int)m_entries.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";

    std::cout << "[AssetLibrary] Saved " << m_entries.size()
              << " assets to " << path << "\n";
}

// ============================================================
// AssetLibrary::load
// ============================================================

void AssetLibrary::load(const std::string& path)
{
    jsonPath = path;
    m_entries.clear();

    std::ifstream f(path);
    if (!f) {
        std::cout << "[AssetLibrary] No library file at " << path
                  << " — starting empty\n";
        return;
    }

    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    // Find each { ... } block inside "assets": [...]
    size_t arrStart = json.find("\"assets\"");
    if (arrStart == std::string::npos) return;
    size_t lb = json.find('[', arrStart);
    if (lb == std::string::npos) return;

    size_t pos = lb + 1;
    while (true) {
        size_t ob = json.find('{', pos);
        if (ob == std::string::npos) break;

        // Find matching closing brace
        int depth = 1;
        size_t cb = ob + 1;
        while (cb < json.size() && depth > 0) {
            if (json[cb] == '{') depth++;
            else if (json[cb] == '}') depth--;
            cb++;
        }
        if (depth != 0) break;

        std::string block = json.substr(ob, cb - ob);

        AssetEntry e;
        e.name       = jsonGetString(block, "name");
        e.sourcePath = jsonGetString(block, "sourcePath");
        e.calibPos   = jsonGetVec3(block, "calibPos");
        e.calibRot   = jsonGetVec3(block, "calibRot");
        e.calibScale = jsonGetVec3(block, "calibScale");

        // Re-import the mesh from disk
        if (!e.sourcePath.empty()) {
            auto asset = importMesh(e.sourcePath);
            if (asset && asset->upload()) {
                e.mesh = asset;
                m_entries.push_back(std::move(e));
                std::cout << "[AssetLibrary] Loaded: " << e.name << "\n";
            } else {
                std::cerr << "[AssetLibrary] Could not reload: "
                          << e.sourcePath << " — skipping\n";
            }
        }

        pos = cb;
    }

    std::cout << "[AssetLibrary] Loaded " << m_entries.size()
              << " assets from " << path << "\n";
}

// ============================================================
// AssetLibrary::importObjs
// ============================================================

std::vector<int> AssetLibrary::importObjs(const std::vector<std::string>& paths)
{
    std::vector<int> newIndices;

    for (auto& path : paths) {
        if (entryExists(path)) {
            std::cout << "[AssetLibrary] Already imported: " << path << "\n";
            continue;
        }

        auto asset = importMesh(path);
        if (!asset || !asset->upload()) continue;

        AssetEntry e;
        e.sourcePath = path;
        e.mesh       = asset;

        // Derive display name from filename
        size_t slash = path.find_last_of("/\\");
        size_t dot   = path.rfind('.');
        e.name = path.substr(
            slash == std::string::npos ? 0 : slash + 1,
            dot   == std::string::npos ? std::string::npos
                                       : dot - (slash == std::string::npos ? 0 : slash + 1));

        // Default calibScale = {1,1,1} — user can adjust per-asset
        e.calibScale = {1.f, 1.f, 1.f};

        newIndices.push_back((int)m_entries.size());
        m_entries.push_back(std::move(e));

        std::cout << "[AssetLibrary] Imported: "
                  << m_entries.back().name << "\n";
    }

    return newIndices;
}

void AssetLibrary::remove(int index)
{
    if (index < 0 || index >= (int)m_entries.size()) return;
    // Free thumbnail texture
    if (m_entries[index].thumbnailTex) {
        glDeleteTextures(1, &m_entries[index].thumbnailTex);
    }
    m_entries.erase(m_entries.begin() + index);
}

bool AssetLibrary::entryExists(const std::string& sourcePath) const
{
    for (auto& e : m_entries)
        if (e.sourcePath == sourcePath) return true;
    return false;
}

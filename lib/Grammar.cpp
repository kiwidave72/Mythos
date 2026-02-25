#include "Grammar.h"
#include <random>
#include <iostream>
#include <sstream>
#include <cmath>

namespace grammar {

// ============================================================
// Library registration
// ============================================================

void Grammar::addPrim(const char* id,
                      glm::vec3   color,
                      std::vector<glm::ivec2> socketDirs,
                      std::function<void()>   drawFn)
{
    PrimDef def;
    def.id    = id;
    def.color = color;
    def.draw  = std::move(drawFn);
    for (auto& d : socketDirs)
        def.sockets.push_back({d});
    m_lib.push_back(std::move(def));
}

const PrimDef* Grammar::findPrim(const char* id) const
{
    for (auto& p : m_lib)
        if (p.id == id) return &p;
    return nullptr;
}

bool Grammar::cellFree(glm::ivec2 c) const
{
    return grid.find({c.x, c.y}) == grid.end();
}

// ============================================================
// Static helpers
// ============================================================

int Grammar::turnSign(glm::ivec2 in, glm::ivec2 out)
{
    return in.x * out.y - in.y * out.x;
}

glm::ivec2 Grammar::getOutDir(const PrimDef* def, glm::ivec2 inDir)
{
    glm::ivec2 entry = -inDir;
    for (auto& s : def->sockets)
        if (s.gridDir != entry) return s.gridDir;
    return {0, 0};
}

// ============================================================
// Blocking generate
// ============================================================

void Grammar::generate(std::function<void(int,int)> progressCb)
{
    if (hardcoded) { generateHardcoded(); return; }

    placed.clear();
    grid.clear();

    const PrimDef* startDef = findPrim("CornerBR");
    if (!startDef) { std::cerr << "[Grammar] CornerBR not registered\n"; return; }

    const int maxAttempts = 2000;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        if (progressCb) progressCb(attempt, maxAttempts);
        if (runAttempt(attempt)) {
            std::cout << "[Grammar] Closed loop: " << placed.size()
                      << " pieces, attempt " << attempt+1 << "\n";
            return;
        }
    }
    std::cerr << "[Grammar] Failed after " << maxAttempts << " attempts\n";
}

// ============================================================
// Step-based generate (for editor animation)
// ============================================================

void Grammar::beginGenerate()
{
    placed.clear();
    grid.clear();
    m_state          = GeneratorState{};
    m_state.running  = true;
    m_state.maxAttempt = 2000;
}

bool Grammar::stepGenerate()
{
    if (!m_state.running) return true;

    if (m_state.attempt >= m_state.maxAttempt) {
        std::cerr << "[Grammar] Failed after " << m_state.maxAttempt << " attempts\n";
        m_state.running = false;
        m_state.failed  = true;
        return true;
    }

    if (runAttempt(m_state.attempt)) {
        std::cout << "[Grammar] Closed loop: " << placed.size()
                  << " pieces, attempt " << m_state.attempt+1 << "\n";
        m_state.running = false;
        m_state.success = true;
        return true;
    }

    m_state.attempt++;
    return false;
}

// ============================================================
// Core attempt logic (shared by blocking + step modes)
// ============================================================

bool Grammar::runAttempt(int attempt)
{
    const PrimDef* startDef = findPrim("CornerBR");
    if (!startDef) return false;

    const glm::ivec2 startHeadDir = startDef->sockets[0].gridDir;
    const glm::ivec2 closeCell    = glm::ivec2(0,0) + startDef->sockets[1].gridDir;
    const glm::ivec2 closeDir     = -startDef->sockets[1].gridDir;

    // Candidate pool — weighted toward straights for longer loops
    static const std::vector<std::string> all = {
        "HStraight","HStraight","HStraight","HStraight",
        "VStraight","VStraight","VStraight","VStraight",
        "CornerTL","CornerTR","CornerBL","CornerBR"
    };

    std::vector<Placed>              tryPlaced;
    std::map<std::pair<int,int>,int> tryGrid;

    tryPlaced.push_back({startDef, {0,0}, 0});
    tryGrid[{0,0}] = 0;

    glm::ivec2 curCell  = startHeadDir;
    glm::ivec2 curDir   = startHeadDir;
    int        netTurns = 1;     // CornerBR contributes +1

    std::mt19937 rng(seed * 1000 + attempt);

    bool success = false;

    for (int step = 0; step < maxPrim * 4 && !success; ++step) {

        // Try to close when we arrive at closeCell.
        // Don't allow closing until we have at least minPrim pieces placed —
        // this forces the loop to be large enough to have interior space.
        bool enoughPieces = (int)tryPlaced.size() >= minPrim;
        if (curCell == closeCell) {
            if (!enoughPieces) {
                // Too few pieces — treat closeCell as blocked, try to route around it
                break;  // dead end this attempt, try again
            }
            glm::ivec2 needed = -curDir;
            for (auto& name : all) {
                const PrimDef* def = findPrim(name.c_str());
                if (!def) continue;
                bool hasEntry = false;
                for (auto& s : def->sockets)
                    if (s.gridDir == needed) { hasEntry = true; break; }
                if (!hasEntry) continue;
                glm::ivec2 outDir = getOutDir(def, curDir);
                if (outDir != closeDir) continue;
                int ts = turnSign(curDir, outDir);
                if (netTurns + ts != 4 && netTurns + ts != -4) continue;
                tryPlaced.push_back({def, curCell, 0});
                tryGrid[{curCell.x, curCell.y}] = (int)tryPlaced.size()-1;
                success = true;
                break;
            }
            break; // stop whether we closed or not
        }

        // Cell already occupied — dead end
        if (tryGrid.find({curCell.x, curCell.y}) != tryGrid.end()) break;

        // Build candidates
        int remaining = maxPrim - (int)tryPlaced.size();
        std::vector<const PrimDef*> candidates;
        for (auto& name : all) {
            const PrimDef* def = findPrim(name.c_str());
            if (!def) continue;
            glm::ivec2 needed = -curDir;
            bool hasEntry = false;
            for (auto& s : def->sockets)
                if (s.gridDir == needed) { hasEntry = true; break; }
            if (!hasEntry) continue;
            glm::ivec2 outDir = getOutDir(def, curDir);
            int ts      = turnSign(curDir, outDir);
            int newNet  = netTurns + ts;
            if (newNet >  4 + remaining) continue;
            if (newNet < -4 - remaining) continue;
            // Reserve closeCell — don't step in prematurely
            if (curCell + outDir == closeCell && (int)tryPlaced.size() < 3) continue;
            candidates.push_back(def);
        }
        if (candidates.empty()) break;

        std::uniform_int_distribution<int> pick(0, (int)candidates.size()-1);
        const PrimDef* def    = candidates[pick(rng)];
        glm::ivec2     outDir = getOutDir(def, curDir);
        int            ts     = turnSign(curDir, outDir);

        tryPlaced.push_back({def, curCell, 0});
        tryGrid[{curCell.x, curCell.y}] = (int)tryPlaced.size()-1;
        netTurns += ts;
        curCell   = curCell + outDir;
        curDir    = outDir;

        // Keep live path in state for animation preview
        m_state.livePath = tryPlaced;
        m_state.curCell  = curCell;
        m_state.curDir   = curDir;
    }

    if (success) {
        // Reject loops that don't enclose meaningful area.
        // Shoelace formula gives signed area of the polygon formed by cell centres.
        // A thin strip or S-bend has area ~0. The hardcoded rectangle has area ~24.
        // Require at least 4 enclosed cells — anything less is a degenerate strip.
        float area = 0.f;
        int n = (int)tryPlaced.size();
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            float xi = (float)tryPlaced[i].cell.x;
            float zi = (float)tryPlaced[i].cell.y;
            float xj = (float)tryPlaced[j].cell.x;
            float zj = (float)tryPlaced[j].cell.y;
            area += xi * zj - xj * zi;
        }
        area = std::abs(area) * 0.5f;

        // Minimum enclosed area based on minPrim:
        // A loop of minPrim pieces around a rectangle of width W, height H
        // has perimeter ~2*(W+H) = minPrim, enclosing area ~(W-2)*(H-2).
        // For minPrim=12 (smallest useful loop): area >= 4
        // For minPrim=20: area >= 12
        // We use minPrim/3 as a reasonable area floor.
        float minArea = std::max(4.f, (float)minPrim / 3.f);
        if (area >= minArea) {
            placed = tryPlaced;
            grid   = tryGrid;
        } else {
            success = false;  // strip or too small — keep trying
        }
    }
    return success;
}

// ============================================================
// Hardcoded demo
// ============================================================

void Grammar::generateHardcoded()
{
    placed.clear(); grid.clear();
    auto place = [&](const char* id, int x, int z) {
        const PrimDef* p = findPrim(id);
        if (!p) return;
        placed.push_back({p, {x,z}, 0});
        grid[{x,z}] = (int)placed.size()-1;
    };
    place("CornerBR", 0,0);
    place("HStraight",1,0); place("HStraight",2,0); place("HStraight",3,0);
    place("HStraight",4,0); place("HStraight",5,0); place("HStraight",6,0);
    place("HStraight",7,0); place("CornerBL",8,0);
    place("VStraight",0,1); place("VStraight",8,1);
    place("VStraight",0,2); place("VStraight",8,2);
    place("CornerTR",0,3);  place("HStraight",1,3); place("CornerBL",2,3);
    place("CornerBR",6,3);  place("HStraight",7,3); place("CornerTL",8,3);
    place("VStraight",2,4); place("VStraight",6,4);
    place("CornerTR",2,5);
    place("HStraight",3,5); place("HStraight",4,5); place("HStraight",5,5);
    place("CornerTL",6,5);
    std::cout << "[Grammar] Hardcoded: " << placed.size() << " pieces\n";
}

// ============================================================
// Serialise
// ============================================================

std::string Grammar::encode() const
{
    std::string out;
    for (int i = 0; i < (int)placed.size(); ++i) {
        if (i > 0) out += "|";
        out += placed[i].def->id;
        out += " ";
        out += std::to_string(placed[i].cell.x);
        out += ",";
        out += std::to_string(placed[i].cell.y);
    }
    return out;
}

bool Grammar::decode(const std::string& s)
{
    placed.clear(); grid.clear();
    std::string tok;
    auto process = [&](const std::string& t) {
        size_t sp = t.find(' ');
        if (sp == std::string::npos) return;
        std::string id   = t.substr(0, sp);
        std::string rest = t.substr(sp+1);
        size_t comma = rest.find(',');
        if (comma == std::string::npos) return;
        int x = std::stoi(rest.substr(0, comma));
        int z = std::stoi(rest.substr(comma+1));
        const PrimDef* def = findPrim(id.c_str());
        if (!def) return;
        placed.push_back({def, {x,z}, 0});
        grid[{x,z}] = (int)placed.size()-1;
    };
    for (char c : s) {
        if (c == '|') { if (!tok.empty()) process(tok); tok.clear(); }
        else tok += c;
    }
    if (!tok.empty()) process(tok);
    return !placed.empty();
}

} // namespace grammar

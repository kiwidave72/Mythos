#include "DPORule.h"
#include <iostream>

namespace merrell {

// ============================================================
// GraphMorphism
// ============================================================

bool GraphMorphism::isTotal(const MerrellGraph& source) const
{
    for (auto& v  : source.vertices)  if (!vertexMap.count(v.id))   return false;
    for (auto& he : source.halfEdges) if (!halfEdgeMap.count(he.id)) return false;
    for (auto& f  : source.faces)     if (!faceMap.count(f.id))      return false;
    return true;
}

bool GraphMorphism::isInjective() const
{
    // Check vertices
    std::unordered_map<int,int> seen;
    for (auto& [src, tgt] : vertexMap) {
        if (seen.count(tgt)) return false;
        seen[tgt] = src;
    }
    seen.clear();
    for (auto& [src, tgt] : halfEdgeMap) {
        if (seen.count(tgt)) return false;
        seen[tgt] = src;
    }
    seen.clear();
    for (auto& [src, tgt] : faceMap) {
        if (seen.count(tgt)) return false;
        seen[tgt] = src;
    }
    return true;
}

// ============================================================
// DPORule
// ============================================================

bool DPORule::isValid() const
{
    // TODO MG-3: implement full validity check
    // - phi_L is total and injective over I → L
    // - phi_R is total and injective over I → R
    // - L, R, I are internally consistent graphs
    return !L.isEmpty() && phi_L.isInjective() && phi_R.isInjective();
}

void DPORule::dump() const
{
    std::cout << "[DPORule " << id << "] \"" << name << "\"";
    switch (kind) {
        case RuleKind::LoopGlue:   std::cout << "  (LoopGlue)";   break;
        case RuleKind::BranchGlue: std::cout << "  (BranchGlue)"; break;
        case RuleKind::Starter:    std::cout << "  (Starter)";     break;
        case RuleKind::Stub:       std::cout << "  (Stub)";        break;
        case RuleKind::General:    std::cout << "  (General)";     break;
    }
    std::cout << "\n";
    std::cout << "  L: "; L.dump();
    std::cout << "  R: "; R.dump();
    std::cout << "  I: "; I.dump();
}

} // namespace merrell

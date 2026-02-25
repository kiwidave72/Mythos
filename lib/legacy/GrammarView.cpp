#include "GrammarView.h"
#include "GrammarInducer.h"
#include "FileDialog.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

// ============================================================
// Init
// ============================================================

void GrammarView::init(Scene& scene, MeshLibrary& lib)
{
    registerPrims();
    startGenerate(scene, lib);
}

void GrammarView::registerPrims()
{
    m_grammar.addPrim("HStraight", {.35f,.62f,.95f}, {{-1,0},{1,0}});
    m_grammar.addPrim("VStraight", {.30f,.85f,.70f}, {{0,-1},{0,1}});
    m_grammar.addPrim("CornerTL",  {.95f,.72f,.25f}, {{-1,0},{0,-1}});
    m_grammar.addPrim("CornerTR",  {.95f,.85f,.35f}, {{ 1,0},{0,-1}});
    m_grammar.addPrim("CornerBL",  {.25f,.85f,.55f}, {{-1,0},{0,1}});
    m_grammar.addPrim("CornerBR",  {.88f,.35f,.72f}, {{ 1,0},{0,1}});
}

void GrammarView::startGenerate(Scene& scene, MeshLibrary& lib)
{
    scene.clear();
    m_grammar.beginGenerate();
    m_animating = true;
}

// ============================================================
// Update
// ============================================================

void GrammarView::update(Scene& scene, MeshLibrary& lib, double /*dt*/)
{
    if (!m_animating) return;

    int steps = m_stepMode ? 1 : m_attemptsPerFrame;
    for (int i = 0; i < steps; ++i) {
        if (m_grammar.stepGenerate()) {
            m_animating = false;
            if (m_grammar.state().success)
                scene.populateFromGrammar(m_grammar, lib);
            break;
        }
    }
}

// ============================================================
// Live path
// ============================================================

void GrammarView::drawLivePath(Renderer& r, const Camera& cam, int w, int h)
{
    if (!m_animating) return;
    const auto& st = m_grammar.state();

    static constexpr float SX = 0.9f, SY = 0.3f, CY = 0.15f;

    for (auto& p : st.livePath) {
        glm::mat4 model = glm::mat4(1.f);
        model = glm::translate(model,
            glm::vec3((float)p.cell.x, CY, (float)p.cell.y));
        model = glm::scale(model, glm::vec3(SX, SY, SX));
        r.drawCube(cam, model, p.def->color * 0.4f, w, h);
    }
    if (!st.livePath.empty()) {
        glm::mat4 model = glm::mat4(1.f);
        model = glm::translate(model,
            glm::vec3((float)st.curCell.x, 0.3f, (float)st.curCell.y));
        model = glm::scale(model, glm::vec3(0.25f));
        r.drawCube(cam, model, {1,1,1}, w, h);
    }
}

// ============================================================
// Panel  — closeable, synced with m_open / showGrammarView
// ============================================================

void GrammarView::drawPanel(Scene& scene, MeshLibrary& lib)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize({300.f, 340.f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({10.f, 70.f},   ImGuiCond_FirstUseEver);

    // Pass &m_open so the X button on the title bar closes the window
    if (!ImGui::Begin("Grammar", &m_open)) {
        ImGui::End();
        return;
    }

    const auto& st = m_grammar.state();
    if (m_animating) {
        ImGui::TextColored({1,0.8f,0.2f,1}, "Generating...");
        ImGui::Text("Attempt %d / %d", st.attempt, st.maxAttempt);
        ImGui::ProgressBar((float)st.attempt/(float)st.maxAttempt, {-1,0});
    } else if (st.success) {
        ImGui::TextColored({0.3f,1,0.3f,1}, "Closed loop found");
        ImGui::Text("%d pieces  (attempt %d)", scene.objectCount(), st.attempt);
    } else if (st.failed) {
        ImGui::TextColored({1,0.3f,0.3f,1}, "Failed — no loop found");
    }

    ImGui::Separator();

    if (ImGui::Button("Generate New", {-1,0}) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        m_grammar.seed += 100;
        startGenerate(scene, lib);
    }
    if (ImGui::Button("Reset Seed", {-1,0})) {
        m_grammar.seed = 42;
        startGenerate(scene, lib);
    }

    ImGui::Separator();
    bool hc = m_grammar.hardcoded;
    if (ImGui::Checkbox("Hardcoded demo", &hc)) {
        m_grammar.hardcoded = hc;
        startGenerate(scene, lib);
    }

    ImGui::Separator();
    ImGui::Text("Animation");
    ImGui::Checkbox("Step mode", &m_stepMode);
    if (!m_stepMode)
        ImGui::SliderInt("Attempts/frame", &m_attemptsPerFrame, 1, 200);
    ImGui::SliderInt("Min pieces", &m_grammar.minPrim, 8, 40);
    ImGui::SliderInt("Max pieces", &m_grammar.maxPrim, 5, 80);
    if (m_grammar.minPrim > m_grammar.maxPrim)
        m_grammar.maxPrim = m_grammar.minPrim;

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Assign OBJ to prim")) {
        static const char* prims[] = {
            "HStraight","VStraight","CornerTL","CornerTR","CornerBL","CornerBR"
        };
        static int selectedPrim = 0;
        ImGui::Combo("Prim", &selectedPrim, prims, 6);

        auto assigned = lib.find("cube:" + std::string(prims[selectedPrim]));
        ImGui::TextDisabled("Current: %s",
            assigned ? assigned->name.c_str() : "procedural cube");

        if (ImGui::Button("Assign imported mesh...")) {
            for (auto& [name, asset] : lib.all()) {
                if (!asset->sourcePath.empty()) {
                    lib.assignObjToPrim(prims[selectedPrim], asset);
                    startGenerate(scene, lib);
                    break;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Layout string")) {
        static char buf[2048] = {};
        std::string enc = m_grammar.encode();
        if (enc.size() < sizeof(buf)-1)
            strncpy(buf, enc.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##layout", buf, sizeof(buf),
                                  {-1,60}, ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Decode"))
            if (m_grammar.decode(buf)) {
                m_animating = false;
                scene.populateFromGrammar(m_grammar, lib);
            }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Learn from Example GEP")) {
        ImGui::TextDisabled("Load a hand-crafted scene to\ninduce a tile grammar from it.");
        if (!m_inducedGrammarPath.empty()) {
            ImGui::TextColored({0.4f,1.f,0.5f,1.f}, "Loaded: %s",
                m_inducedGrammarPath.c_str());
            ImGui::Text("%d variants  %d rules  %d edges",
                (int)m_inducedGrammar.tileVariants.size(),
                (int)m_inducedGrammar.rules.size(),
                (int)m_inducedGrammar.edges.size());
        }
        if (ImGui::Button("Load example GEP...", {-1,0})) {
            auto paths = FileDialog::openFiles(
                "Open Example Scene",
                {{"Graph Editor Project","*.gep"},{"All Files","*.*"}},
                "gep");
            if (!paths.empty()) {
                auto g = grammar::GrammarInducer::induceFromFile(paths[0]);
                if (!g.tileVariants.empty()) {
                    m_inducedGrammar     = std::move(g);
                    m_inducedGrammarPath = paths[0];
                    std::cout << "\n=== INDUCED GRAMMAR ===\n"
                              << m_inducedGrammar.toJson() << "\n";
                }
            }
        }
    }

    ImGui::End();
}

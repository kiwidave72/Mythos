#pragma once
#include "Grammar.h"
#include "GrammarInducer.h"
#include "Scene.h"
#include "Renderer.h"
#include <glm/glm.hpp>

class GrammarView
{
public:
    struct Settings {
        int  seed      = 42;
        int  minPrim   = 16;
        int  maxPrim   = 60;
        bool hardcoded = false;
    };

    void init(Scene& scene, MeshLibrary& lib);
    void drawPanel(Scene& scene, MeshLibrary& lib);
    void update(Scene& scene, MeshLibrary& lib, double dt);
    void drawLivePath(Renderer& r, const Camera& cam, int w, int h);

    bool isGenerating() const { return m_animating; }

    // Open / close (mirrors AssetLibraryView pattern)
    bool isOpen()           const { return m_open; }
    void setOpen(bool open)       { m_open = open; }

    Settings settings() const {
        return { m_grammar.seed, m_grammar.minPrim,
                 m_grammar.maxPrim, m_grammar.hardcoded };
    }
    void applySettings(const Settings& s) {
        m_grammar.seed      = s.seed;
        m_grammar.minPrim   = s.minPrim;
        m_grammar.maxPrim   = s.maxPrim;
        m_grammar.hardcoded = s.hardcoded;
    }
    void stopGenerating() { m_animating = false; }

private:
    grammar::Grammar          m_grammar;
    grammar::InducedGrammar   m_inducedGrammar;
    std::string               m_inducedGrammarPath;
    bool  m_animating        = false;
    bool  m_stepMode         = false;
    bool  m_open             = true;   // window visibility
    int   m_attemptsPerFrame = 10;

    void startGenerate(Scene& scene, MeshLibrary& lib);
    void registerPrims();
};

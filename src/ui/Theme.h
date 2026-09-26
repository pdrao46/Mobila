#pragma once
#include <imgui.h>

namespace mob {
// Identidade visual do MOBILADOR: escuro, contraste alto, acentos ciano->violeta.
struct Palette {
    ImVec4 bg, panel, card, cardHover, border, text, muted, accent, accent2, success, warn, danger;
};
const Palette& pal();
void applyTheme(bool dark, float scale);
ImU32 col(const ImVec4& c, float alpha = 1.0f);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* title = nullptr;
    ImFont* hero = nullptr;
    ImFont* mono = nullptr;
    bool icons = false;
};
Fonts& fonts();
void buildFonts(float scale);

// Ícones Segoe MDL2/Fluent (presentes no Windows 10/11). Vazio se a fonte não existir.
namespace icon {
const char* home();
const char* play();
const char* keyboard();
const char* perf();
const char* bench();
const char* settings();
const char* logs();
const char* phone();
const char* usb();
const char* mouse();
const char* bolt();
}

// Componentes
void drawLogo(ImDrawList* dl, ImVec2 pos, float size, float t = 0.f);
bool primaryButton(const char* label, ImVec2 size, bool danger = false);
void statusDot(bool ok, bool warn = false);
bool beginCard(const char* id, ImVec2 size = ImVec2(0, 0));
void endCard();
void metricTile(const char* label, const char* value, const char* hint = nullptr, ImVec4* color = nullptr);
void sectionTitle(const char* t, const char* sub = nullptr);
}

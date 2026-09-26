#include "Theme.h"
#include <imgui_internal.h>
#include "../core/Platform.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace mob {
static Palette g_dark = {
    {0.043f, 0.059f, 0.090f, 1}, {0.063f, 0.082f, 0.122f, 1}, {0.082f, 0.106f, 0.157f, 1},
    {0.102f, 0.133f, 0.196f, 1}, {0.141f, 0.176f, 0.251f, 1}, {0.902f, 0.918f, 0.949f, 1},
    {0.541f, 0.580f, 0.659f, 1}, {0.000f, 0.898f, 1.000f, 1}, {0.486f, 0.302f, 1.000f, 1},
    {0.133f, 0.827f, 0.604f, 1}, {1.000f, 0.690f, 0.125f, 1}, {1.000f, 0.302f, 0.416f, 1}};
static Palette g_light = {
    {0.953f, 0.961f, 0.976f, 1}, {1.0f, 1.0f, 1.0f, 1}, {1.0f, 1.0f, 1.0f, 1},
    {0.937f, 0.949f, 0.973f, 1}, {0.855f, 0.878f, 0.918f, 1}, {0.075f, 0.094f, 0.137f, 1},
    {0.380f, 0.420f, 0.502f, 1}, {0.000f, 0.588f, 0.780f, 1}, {0.408f, 0.235f, 0.902f, 1},
    {0.047f, 0.600f, 0.420f, 1}, {0.851f, 0.537f, 0.000f, 1}, {0.886f, 0.180f, 0.290f, 1}};
static const Palette* g_pal = &g_dark;
static Fonts g_fonts;

const Palette& pal() { return *g_pal; }
Fonts& fonts() { return g_fonts; }
ImU32 col(const ImVec4& c, float a) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

void applyTheme(bool dark, float scale) {
    g_pal = dark ? &g_dark : &g_light;
    const Palette& p = *g_pal;
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 10; st.ChildRounding = 10; st.FrameRounding = 7; st.PopupRounding = 8;
    st.GrabRounding = 7; st.TabRounding = 7; st.ScrollbarRounding = 8;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 0; st.PopupBorderSize = 1;
    st.WindowPadding = ImVec2(18, 16); st.FramePadding = ImVec2(10, 7); st.ItemSpacing = ImVec2(10, 9);
    st.ItemInnerSpacing = ImVec2(8, 6); st.ScrollbarSize = 12; st.GrabMinSize = 12;
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.muted;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = p.card;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_FrameBg] = dark ? ImVec4(0.11f, 0.14f, 0.21f, 1) : ImVec4(0.93f, 0.94f, 0.96f, 1);
    c[ImGuiCol_FrameBgHovered] = dark ? ImVec4(0.14f, 0.18f, 0.26f, 1) : ImVec4(0.89f, 0.91f, 0.95f, 1);
    c[ImGuiCol_FrameBgActive] = dark ? ImVec4(0.16f, 0.21f, 0.30f, 1) : ImVec4(0.85f, 0.88f, 0.93f, 1);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = p.panel;
    c[ImGuiCol_Button] = dark ? ImVec4(0.13f, 0.17f, 0.25f, 1) : ImVec4(0.90f, 0.92f, 0.95f, 1);
    c[ImGuiCol_ButtonHovered] = dark ? ImVec4(0.17f, 0.22f, 0.32f, 1) : ImVec4(0.85f, 0.88f, 0.93f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.45f);
    c[ImGuiCol_Header] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.16f);
    c[ImGuiCol_HeaderHovered] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.24f);
    c[ImGuiCol_HeaderActive] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.32f);
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent2;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_Tab] = c[ImGuiCol_Button];
    c[ImGuiCol_TabHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_TabSelected] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.30f);
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_TableHeaderBg] = p.panel;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = p.border;
    c[ImGuiCol_TableRowBgAlt] = dark ? ImVec4(1, 1, 1, 0.025f) : ImVec4(0, 0, 0, 0.025f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_NavHighlight] = p.accent;
    st.ScaleAllSizes(scale);
    ImGui::GetStyle() = st;
}

void buildFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // Latin-1 + pontuação geral (— • ‹ …) + setas + símbolos de grau
    static const ImWchar latinRanges[] = {0x0020, 0x00FF, 0x2000, 0x206F, 0x2190, 0x21FF, 0x2300, 0x23FF, 0x25A0, 0x25FF, 0};
    [[maybe_unused]] const ImWchar* latin = latinRanges;
    [[maybe_unused]] static const ImWchar iconRange[] = {0xE700, 0xF8FF, 0};
    auto load = [&](const char* file, float px, bool withIcons) -> ImFont* {
        ImFont* f = nullptr;
#ifdef _WIN32
        std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (fileExists(path)) f = io.Fonts->AddFontFromFileTTF(path.c_str(), px * scale, nullptr, latin);
#else
        std::string fn = file, base = "/usr/share/fonts/truetype/dejavu/";
        std::string path = base + (fn == "consola.ttf" ? "DejaVuSansMono.ttf" : fn == "segoeui.ttf" ? "DejaVuSans.ttf" : "DejaVuSans-Bold.ttf");
        if (fileExists(path)) f = io.Fonts->AddFontFromFileTTF(path.c_str(), px * scale * 0.92f, nullptr, latin);
#endif
        if (!f) {
            ImFontConfig cfg;
            cfg.SizePixels = px * scale;
            f = io.Fonts->AddFontDefault(&cfg);
        }
#ifdef _WIN32
        if (withIcons) {
            for (const char* ic : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"}) {
                if (!fileExists(ic)) continue;
                ImFontConfig cfg;
                cfg.MergeMode = true;
                cfg.GlyphOffset = ImVec2(0, 2 * scale);
                io.Fonts->AddFontFromFileTTF(ic, px * scale, &cfg, iconRange);
                g_fonts.icons = true;
                break;
            }
        }
#else
        (void)withIcons;
#endif
        return f;
    };
    g_fonts.body = load("segoeui.ttf", 16, true);
    g_fonts.bold = load("seguisb.ttf", 16, true);
    g_fonts.title = load("seguisb.ttf", 22, true);
    g_fonts.hero = load("segoeuib.ttf", 46, false);
    g_fonts.mono = load("consola.ttf", 14.5f, false);
}

namespace icon {
static const char* ic(const char* s) { return fonts().icons ? s : ""; }
const char* home() { return ic("\xEE\xA0\x8F  "); }      // E80F
const char* play() { return ic("\xEE\x9F\xBC  "); }      // E7FC (Game)
const char* keyboard() { return ic("\xEE\x9D\xA5  "); }  // E765
const char* perf() { return ic("\xEE\xA7\x99  "); }      // E9D9
const char* bench() { return ic("\xEE\xA7\x92  "); }     // E9D2
const char* settings() { return ic("\xEE\x9C\x93  "); }  // E713
const char* logs() { return ic("\xEE\xA8\xB7  "); }      // EA37
const char* phone() { return ic("\xEE\xA3\xAA  "); }     // E8EA
const char* usb() { return ic("\xEE\xB3\xB0  "); }       // ECF0
const char* mouse() { return ic("\xEE\xA5\xA2  "); }     // E962
const char* bolt() { return ic("\xEE\xA5\x85  "); }      // E945
}

void drawLogo(ImDrawList* dl, ImVec2 p, float s, float t) {
    const Palette& P = pal();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(16, 21, 33, 255), s * 0.24f);
    dl->AddRect(p, ImVec2(p.x + s, p.y + s), col(P.border), s * 0.24f, 0, s * 0.02f);
    ImVec2 pts[5] = {{p.x + s * 0.24f, p.y + s * 0.72f}, {p.x + s * 0.24f, p.y + s * 0.29f},
                     {p.x + s * 0.50f, p.y + s * 0.55f}, {p.x + s * 0.76f, p.y + s * 0.29f},
                     {p.x + s * 0.76f, p.y + s * 0.72f}};
    float glow = 0.5f + 0.5f * std::sin(t * 3.0f);
    dl->AddPolyline(pts, 5, col(P.accent, 0.18f + 0.12f * glow), 0, s * 0.19f);
    // gradiente ciano -> violeta por segmento
    for (int i = 0; i < 4; ++i) {
        float a = i / 3.0f;
        ImVec4 c(P.accent.x + (P.accent2.x - P.accent.x) * a, P.accent.y + (P.accent2.y - P.accent.y) * a,
                 P.accent.z + (P.accent2.z - P.accent.z) * a, 1);
        dl->AddLine(pts[i], pts[i + 1], col(c), s * 0.105f);
        dl->AddCircleFilled(pts[i], s * 0.0525f, col(c));
    }
    dl->AddCircleFilled(pts[4], s * 0.0525f, col(P.accent2));
    dl->AddRectFilled(ImVec2(p.x + s * 0.12f, p.y + s * 0.79f), ImVec2(p.x + s * 0.41f, p.y + s * 0.815f),
                      col(P.accent, 0.85f), s);
    dl->AddRectFilled(ImVec2(p.x + s * 0.17f, p.y + s * 0.845f), ImVec2(p.x + s * 0.37f, p.y + s * 0.87f),
                      col(P.accent, 0.5f), s);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.83f, p.y + s * 0.83f), s * 0.03f, col(P.accent2));
}

bool primaryButton(const char* label, ImVec2 size, bool danger) {
    const Palette& P = pal();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 a = danger ? P.danger : P.accent, b = danger ? ImVec4(0.8f, 0.2f, 0.5f, 1) : P.accent2;
    float k = act ? 0.8f : (hov ? 1.1f : 1.0f);
    auto mul = [&](ImVec4 c) { return ImVec4(std::fmin(1.f, c.x * k), std::fmin(1.f, c.y * k), std::fmin(1.f, c.z * k), 1); };
    float r = size.y * 0.5f;
    if (hov) dl->AddRectFilled(ImVec2(pos.x - 3, pos.y - 3), ImVec2(pos.x + size.x + 3, pos.y + size.y + 3), col(a, 0.18f), r + 3);
    int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32_WHITE, r);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, pos, ImVec2(pos.x + size.x, pos.y),
                                                  col(mul(a)), col(mul(b)));
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImGui::PushFont(fonts().bold);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), IM_COL32(8, 12, 20, 255), label, end);
    ImGui::PopFont();
    return pressed;
}

void statusDot(bool ok, bool warn) {
    const Palette& P = pal();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImVec4 c = ok ? P.success : (warn ? P.warn : P.muted);
    float t = (float)ImGui::GetTime();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ok) dl->AddCircleFilled(ImVec2(p.x + h * 0.35f, p.y + h * 0.55f), h * (0.30f + 0.08f * std::sin(t * 4)), col(c, 0.25f));
    dl->AddCircleFilled(ImVec2(p.x + h * 0.35f, p.y + h * 0.55f), h * 0.2f, col(c));
    ImGui::Dummy(ImVec2(h * 0.8f, h));
    ImGui::SameLine();
}

bool beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().WindowPadding);
    return ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                                          (size.y == 0 ? ImGuiChildFlags_AutoResizeY : 0),
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void metricTile(const char* label, const char* value, const char* hint, ImVec4* color) {
    ImGui::TextColored(pal().muted, "%s", label);
    ImGui::PushFont(fonts().title);
    if (color) ImGui::TextColored(*color, "%s", value);
    else ImGui::TextUnformatted(value);
    ImGui::PopFont();
    if (hint) ImGui::TextColored(pal().muted, "%s", hint);
}

void sectionTitle(const char* t, const char* sub) {
    ImGui::PushFont(fonts().title);
    ImGui::TextUnformatted(t);
    ImGui::PopFont();
    if (sub) ImGui::TextColored(pal().muted, "%s", sub);
    ImGui::Spacing();
}
}

#include "App.h"
#include "Theme.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mob {

// Transformação vídeo(normalizado) -> tela, considerando rotação aplicada no PC.
struct Xf {
    ImVec2 o, s;
    int rot;
    ImVec2 pt(float x, float y) const {
        float u = x, v = y;
        switch (rot) {
        case 90: u = 1 - y; v = x; break;
        case 180: u = 1 - x; v = 1 - y; break;
        case 270: u = y; v = 1 - x; break;
        default: break;
        }
        return ImVec2(o.x + u * s.x, o.y + v * s.y);
    }
    float vidW() const { return (rot == 90 || rot == 270) ? s.y : s.x; }  // largura do vídeo em px de tela
    float vidH() const { return (rot == 90 || rot == 270) ? s.x : s.y; }
};

static std::string keyLabel(const Binding& b) {
    std::string k = b.toString();
    if (k == "Mouse1") return "LMB";
    if (k == "Mouse3") return "RMB";
    if (k == "Mouse2") return "MMB";
    if (k == "Left Shift") return "Shift";
    if (k == "Space") return "Espaço";
    return k;
}

void App::drawOverlayControls(ImDrawList* dl, ImVec2 origin, ImVec2 size, bool editing) {
    Profile* p = activeProfile();
    if (!p) return;
    const Palette& P = pal();
    Xf xf{origin, size, editing ? 0 : cfg_.video.rotation};
    float a = editing ? 1.0f : 0.55f;
    ImU32 ring = col(P.accent, 0.9f * a), fill = col(ImVec4(0, 0, 0, 1), 0.35f * a), txt = col(ImVec4(1, 1, 1, 1), a);
    for (size_t i = 0; i < p->elements.size(); ++i) {
        const Element& e = p->elements[i];
        bool act = !editing && mapper_->isElementActive(i);
        bool sel = editing && (int)i == selElem_;
        ImU32 rc = sel ? col(P.accent2) : ring;
        ImU32 fc = act ? col(P.accent, 0.45f) : fill;
        ImVec2 c = xf.pt(e.x, e.y);
        float r = e.r * xf.vidW();
        switch (e.type) {
        case ElemType::Joystick: {
            dl->AddCircleFilled(c, r, fc, 48);
            dl->AddCircle(c, r, rc, 48, 2.0f);
            dl->AddCircleFilled(c, r * 0.35f, col(P.accent, 0.35f * a), 32);
            auto lab = [&](const Binding& b, float dx, float dy) {
                std::string s = keyLabel(b);
                ImVec2 ts = ImGui::CalcTextSize(s.c_str());
                dl->AddText(ImVec2(c.x + dx * r * 0.68f - ts.x / 2, c.y + dy * r * 0.68f - ts.y / 2), txt, s.c_str());
            };
            lab(e.up, 0, -1); lab(e.down, 0, 1); lab(e.left, -1, 0); lab(e.right, 1, 0);
            break;
        }
        case ElemType::Camera:
        case ElemType::MoveArea: {
            ImVec2 p0 = xf.pt(e.x - e.w / 2, e.y - e.h / 2), p1 = xf.pt(e.x + e.w / 2, e.y + e.h / 2);
            ImVec2 mn(std::min(p0.x, p1.x), std::min(p0.y, p1.y)), mx(std::max(p0.x, p1.x), std::max(p0.y, p1.y));
            if (editing || act) dl->AddRectFilled(mn, mx, col(e.type == ElemType::Camera ? P.accent : P.accent2, 0.07f));
            // borda tracejada
            ImU32 dc = col(e.type == ElemType::Camera ? P.accent : P.accent2, 0.7f * a);
            float dash = 8;
            for (float x = mn.x; x < mx.x; x += dash * 2) {
                dl->AddLine(ImVec2(x, mn.y), ImVec2(std::min(x + dash, mx.x), mn.y), dc, sel ? 2.f : 1.f);
                dl->AddLine(ImVec2(x, mx.y), ImVec2(std::min(x + dash, mx.x), mx.y), dc, sel ? 2.f : 1.f);
            }
            for (float y = mn.y; y < mx.y; y += dash * 2) {
                dl->AddLine(ImVec2(mn.x, y), ImVec2(mn.x, std::min(y + dash, mx.y)), dc, sel ? 2.f : 1.f);
                dl->AddLine(ImVec2(mx.x, y), ImVec2(mx.x, std::min(y + dash, mx.y)), dc, sel ? 2.f : 1.f);
            }
            std::string s = e.label + (e.type == ElemType::MoveArea && !e.key.empty() ? " [" + keyLabel(e.key) + "]" : "");
            dl->AddText(ImVec2(mn.x + 6, mn.y + 4), dc, s.c_str());
            if (e.type == ElemType::Camera) dl->AddCircle(c, 6, dc, 16, 1.5f);
            break;
        }
        case ElemType::Swipe: {
            ImVec2 d = xf.pt(e.x2, e.y2);
            dl->AddLine(c, d, rc, 2.5f);
            ImVec2 dir(d.x - c.x, d.y - c.y);
            float L = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (L > 1) {
                dir.x /= L; dir.y /= L;
                ImVec2 n(-dir.y, dir.x);
                dl->AddTriangleFilled(d, ImVec2(d.x - dir.x * 12 + n.x * 6, d.y - dir.y * 12 + n.y * 6),
                                      ImVec2(d.x - dir.x * 12 - n.x * 6, d.y - dir.y * 12 - n.y * 6), rc);
            }
            if (editing) dl->AddCircleFilled(d, 5, col(P.accent2));
        }
            [[fallthrough]];
        default: {
            dl->AddCircleFilled(c, r, fc, 40);
            dl->AddCircle(c, r, rc, 40, e.type == ElemType::Aim ? 2.5f : 2.0f);
            if (e.type == ElemType::Aim) {
                dl->AddLine(ImVec2(c.x - r * 0.5f, c.y), ImVec2(c.x + r * 0.5f, c.y), rc, 1.5f);
                dl->AddLine(ImVec2(c.x, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.5f), rc, 1.5f);
            }
            if (e.type == ElemType::Tap) dl->AddCircle(c, r * 0.6f, rc, 32, 1.0f);
            std::string s = keyLabel(e.key);
            ImVec2 ts = ImGui::CalcTextSize(s.c_str());
            dl->AddText(ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), txt, s.c_str());
            break;
        }
        }
        if (editing && e.type != ElemType::Camera && e.type != ElemType::MoveArea) {
            ImVec2 ts = ImGui::CalcTextSize(e.label.c_str());
            dl->AddText(ImVec2(c.x - ts.x / 2, c.y + r + 2), col(P.muted), e.label.c_str());
        }
    }
}

static Element makeElement(ElemType t) {
    Element e;
    e.type = t;
    e.label = elemTypeName(t);
    switch (t) {
    case ElemType::Joystick:
        e.r = 0.07f; e.x = 0.18f; e.y = 0.7f;
        e.up = Binding::parse("W"); e.down = Binding::parse("S"); e.left = Binding::parse("A"); e.right = Binding::parse("D");
        break;
    case ElemType::Camera: e.w = 0.45f; e.h = 0.55f; e.x = 0.62f; e.y = 0.45f; break;
    case ElemType::MoveArea: e.w = 0.3f; e.h = 0.3f; e.key = Binding::parse("Left Alt"); break;
    case ElemType::Swipe: e.x2 = 0.5f; e.y2 = 0.3f; e.key = Binding::parse("Q"); break;
    case ElemType::Aim: e.key = Binding::parse("Mouse3"); e.holdMode = false; break;
    default: e.key = Binding::parse("E"); break;
    }
    return e;
}

void App::viewEditor() {
    const Palette& P = pal();
    float s = fontScaleBuilt_;
    sectionTitle("Editor de mapeamento", "Posicione os controles sobre a tela real do celular. Arraste para mover, roda do mouse para redimensionar.");
    float totalW = ImGui::GetContentRegionAvail().x;
    float leftW = std::min(220 * s, totalW * 0.18f), rightW = std::min(330 * s, totalW * 0.27f);
    float availH = ImGui::GetContentRegionAvail().y;

    // ---- perfis
    ImGui::BeginChild("##profiles", ImVec2(leftW, availH), ImGuiChildFlags_Borders);
    ImGui::PushFont(fonts().bold); ImGui::TextUnformatted("Perfis"); ImGui::PopFont();
    std::string toSelect;
    for (auto& p : profiles_->all())
        if (ImGui::Selectable(p.name.c_str(), p.name == cfg_.activeProfile)) toSelect = p.name;
    if (!toSelect.empty()) {
        capturing_ = nullptr;
        selElem_ = -1;
        cfg_.activeProfile = toSelect;
        mapper_->setProfile(activeProfile());
        saveSettings();
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##np", "nome do perfil", newProfileName_, sizeof newProfileName_);
    bool nameOk = newProfileName_[0] && !profiles_->find(newProfileName_);
    ImGui::BeginDisabled(!nameOk);
    auto afterChange = [&](const std::string& name) {
        capturing_ = nullptr;
        selElem_ = -1;
        cfg_.activeProfile = name;
        newProfileName_[0] = 0;
        mapper_->setProfile(activeProfile());  // vetor de perfis pode ter realocado
        saveSettings();
    };
    if (ImGui::Button("Novo", ImVec2(-1, 0))) { std::string n = newProfileName_; profiles_->create(n); afterChange(n); }
    if (ImGui::Button("Duplicar atual", ImVec2(-1, 0))) { std::string n = newProfileName_; profiles_->duplicate(cfg_.activeProfile, n); afterChange(n); }
    if (ImGui::Button("Renomear atual", ImVec2(-1, 0)) && activeProfile()) {
        Profile copy = *activeProfile();
        profiles_->remove(copy.name);
        copy.name = newProfileName_;
        profiles_->all().push_back(copy);
        profiles_->save(copy);
        afterChange(copy.name);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(profiles_->all().size() <= 1);
    if (ImGui::Button("Excluir atual", ImVec2(-1, 0))) {
        profiles_->remove(cfg_.activeProfile);
        afterChange(profiles_->all().front().name);
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Recarregar do disco", ImVec2(-1, 0))) { profiles_->loadAll(); afterChange(profiles_->find(cfg_.activeProfile) ? cfg_.activeProfile : profiles_->all().front().name); }
    ImGui::TextColored(P.muted, "Arquivos .mobmap.json em:");
    ImGui::TextWrapped("%s", profiles_->dir().c_str());
    ImGui::EndChild();

    Profile* prof = activeProfile();
    if (!prof) return;
    ImGui::SameLine();

    // ---- canvas
    float canvasW = ImGui::GetContentRegionAvail().x - rightW - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("##canvas", ImVec2(canvasW, availH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // barra de ferramentas
    ElemType types[] = {ElemType::Button, ElemType::Joystick, ElemType::Tap, ElemType::Camera, ElemType::Aim, ElemType::Swipe, ElemType::MoveArea};
    for (auto t : types) {
        char b[48];
        std::snprintf(b, sizeof b, "+ %s", elemTypeName(t));
        float bw = ImGui::CalcTextSize(b).x + ImGui::GetStyle().FramePadding.x * 2;
        if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1 &&
            ImGui::GetCursorPosX() + bw > ImGui::GetWindowContentRegionMax().x) ImGui::NewLine();
        if (ImGui::Button(b)) {
            capturing_ = nullptr;
            prof->elements.push_back(makeElement(t));
            selElem_ = (int)prof->elements.size() - 1;
            profiles_->save(*prof);
            mapper_->setProfile(prof);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    int vw = session_->videoWidth(), vh = session_->videoHeight();
    float aspect = (tex_ && vw && vh) ? (float)vw / vh : 20.f / 9.f;
    ImVec2 av = ImGui::GetContentRegionAvail();
    float cw = av.x, ch = av.x / aspect;
    if (ch > av.y) { ch = av.y; cw = ch * aspect; }
    ImVec2 o = ImGui::GetCursorScreenPos();
    o.x += (av.x - cw) / 2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tex_ && vw) {
        dl->AddImage((ImTextureID)(uintptr_t)tex_, o, ImVec2(o.x + cw, o.y + ch));
    } else {
        dl->AddRectFilledMultiColor(o, ImVec2(o.x + cw, o.y + ch), IM_COL32(18, 24, 38, 255), IM_COL32(28, 22, 48, 255),
                                    IM_COL32(14, 18, 30, 255), IM_COL32(12, 16, 26, 255));
        const char* m = "Conecte o celular para ver a tela real do jogo aqui";
        ImVec2 ts = ImGui::CalcTextSize(m);
        dl->AddText(ImVec2(o.x + (cw - ts.x) / 2, o.y + (ch - ts.y) / 2), col(P.muted), m);
    }
    dl->AddRect(o, ImVec2(o.x + cw, o.y + ch), col(P.border), 6);
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##bg", ImVec2(cw, ch));
    if (ImGui::IsItemClicked()) selElem_ = -1;
    bool canvasHovered = ImGui::IsItemHovered();

    // interação com elementos (ordem inversa: o de cima recebe o clique)
    static bool dirty = false;
    ImGuiIO& io = ImGui::GetIO();
    for (int i = (int)prof->elements.size() - 1; i >= 0; --i) {
        Element& e = prof->elements[i];
        bool area = e.type == ElemType::Camera || e.type == ElemType::MoveArea;
        ImVec2 c(o.x + e.x * cw, o.y + e.y * ch);
        ImVec2 half = area ? ImVec2(e.w * cw / 2, e.h * ch / 2) : ImVec2(e.r * cw, e.r * cw);
        ImGui::SetCursorScreenPos(ImVec2(c.x - half.x, c.y - half.y));
        ImGui::PushID(i);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##el", ImVec2(std::max(8.f, half.x * 2), std::max(8.f, half.y * 2)));
        if (ImGui::IsItemActivated()) { selElem_ = i; capturing_ = nullptr; }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0)) {
            e.x = std::clamp(e.x + io.MouseDelta.x / cw, 0.f, 1.f);
            e.y = std::clamp(e.y + io.MouseDelta.y / ch, 0.f, 1.f);
            if (e.type == ElemType::Swipe) { e.x2 = std::clamp(e.x2 + io.MouseDelta.x / cw, 0.f, 1.f); e.y2 = std::clamp(e.y2 + io.MouseDelta.y / ch, 0.f, 1.f); }
            dirty = true;
        }
        if (ImGui::IsItemHovered() && io.MouseWheel != 0 && i == selElem_) {
            float k = 1.f + io.MouseWheel * 0.06f;
            if (area) { e.w = std::clamp(e.w * k, 0.05f, 1.f); e.h = std::clamp(e.h * k, 0.05f, 1.f); }
            else e.r = std::clamp(e.r * k, 0.01f, 0.25f);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s — %s", elemTypeName(e.type), e.label.c_str());
        // alças: redimensionar e destino do swipe
        if (i == selElem_) {
            ImVec2 hp = area ? ImVec2(c.x + half.x, c.y + half.y) : ImVec2(c.x + half.x, c.y);
            ImGui::SetCursorScreenPos(ImVec2(hp.x - 6, hp.y - 6));
            ImGui::InvisibleButton("##rs", ImVec2(12, 12));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0)) {
                if (area) { e.w = std::clamp(e.w + io.MouseDelta.x * 2 / cw, 0.05f, 1.f); e.h = std::clamp(e.h + io.MouseDelta.y * 2 / ch, 0.05f, 1.f); }
                else e.r = std::clamp(e.r + io.MouseDelta.x / cw, 0.01f, 0.25f);
                dirty = true;
            }
            ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(hp.x - 5, hp.y - 5), ImVec2(hp.x + 5, hp.y + 5), col(P.accent2), 2);
            if (e.type == ElemType::Swipe) {
                ImVec2 d(o.x + e.x2 * cw, o.y + e.y2 * ch);
                ImGui::SetCursorScreenPos(ImVec2(d.x - 7, d.y - 7));
                ImGui::InvisibleButton("##sw", ImVec2(14, 14));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0)) {
                    e.x2 = std::clamp(e.x2 + io.MouseDelta.x / cw, 0.f, 1.f);
                    e.y2 = std::clamp(e.y2 + io.MouseDelta.y / ch, 0.f, 1.f);
                    dirty = true;
                }
            }
        }
        ImGui::PopID();
    }
    drawOverlayControls(dl, o, ImVec2(cw, ch), true);
    if (canvasHovered && selElem_ >= 0 && !capturing_ && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        prof->elements.erase(prof->elements.begin() + selElem_);
        selElem_ = -1;
        dirty = true;
    }
    if (dirty && !ImGui::IsMouseDown(0)) {
        profiles_->save(*prof);
        mapper_->setProfile(prof);
        dirty = false;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- propriedades
    ImGui::BeginChild("##props", ImVec2(rightW, availH), ImGuiChildFlags_Borders);
    ImGui::PushFont(fonts().bold); ImGui::TextUnformatted("Propriedades"); ImGui::PopFont();
    if (selElem_ < 0 || selElem_ >= (int)prof->elements.size()) {
        ImGui::TextColored(P.muted, "Selecione um controle no canvas.");
        ImGui::Separator();
        ImGui::TextColored(P.muted, "%zu controles neste perfil.", prof->elements.size());
        bool ch2 = ImGui::SliderFloat("Sensibilidade global", &prof->globalSensitivity, 0.1f, 5.f, "%.2f");
        if (ch2) dirty = true;
        ImGui::TextWrapped("Dica: botão esquerdo do mouse = \"Mouse1\", direito = \"Mouse3\". Para combinações, marque Ctrl/Shift/Alt.");
        ImGui::EndChild();
        return;
    }
    Element& e = prof->elements[selElem_];
    bool ch2 = false;
    ImGui::TextColored(P.accent, "%s", elemTypeName(e.type));
    char lbl[64];
    std::snprintf(lbl, sizeof lbl, "%s", e.label.c_str());
    if (ImGui::InputText("Nome", lbl, sizeof lbl)) { e.label = lbl; ch2 = true; }
    auto bind = [&](const char* name, Binding& b, bool mods) {
        ImGui::PushID(name);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::SameLine(110 * s);
        std::string before = b.key;
        keyCaptureButton(name, b.key, true);
        if (b.key != before) ch2 = true;
        if (mods) {
            ImGui::Dummy(ImVec2(100 * s, 0)); ImGui::SameLine();
            ch2 |= ImGui::Checkbox("Ctrl", &b.ctrl); ImGui::SameLine();
            ch2 |= ImGui::Checkbox("Shift", &b.shift); ImGui::SameLine();
            ch2 |= ImGui::Checkbox("Alt", &b.alt);
            ImGui::Dummy(ImVec2(100 * s, 0)); ImGui::SameLine();
            if (ImGui::SmallButton("Mouse1")) { b.key = "Mouse1"; ch2 = true; } ImGui::SameLine();
            if (ImGui::SmallButton("Mouse2")) { b.key = "Mouse2"; ch2 = true; } ImGui::SameLine();
            if (ImGui::SmallButton("Mouse3")) { b.key = "Mouse3"; ch2 = true; }
        }
        ImGui::PopID();
    };
    // mudanças de tecla capturadas em outro frame também precisam salvar
    static std::string lastSig;
    std::string sig = e.key.toString() + e.up.key + e.down.key + e.left.key + e.right.key + e.modifierSlow.key;
    if (sig != lastSig) { lastSig = sig; ch2 = true; }
    switch (e.type) {
    case ElemType::Joystick:
        bind("Cima", e.up, false); bind("Baixo", e.down, false); bind("Esquerda", e.left, false); bind("Direita", e.right, false);
        bind("Andar (lento)", e.modifierSlow, false);
        ch2 |= ImGui::SliderFloat("Raio", &e.r, 0.02f, 0.25f, "%.3f");
        break;
    case ElemType::Camera:
        ch2 |= ImGui::SliderFloat("Sensibilidade", &e.sensitivity, 0.05f, 5.f, "%.2f");
        ch2 |= ImGui::SliderFloat("Mult. X", &e.multX, 0.1f, 3.f, "%.2f");
        ch2 |= ImGui::SliderFloat("Mult. Y", &e.multY, 0.1f, 3.f, "%.2f");
        ch2 |= ImGui::SliderFloat("Aceleração", &e.accel, 0.f, 2.f, "%.2f");
        ch2 |= ImGui::SliderFloat("Deadzone", &e.deadzone, 0.f, 5.f, "%.1f");
        ch2 |= ImGui::Checkbox("Inverter X", &e.invertX); ImGui::SameLine();
        ch2 |= ImGui::Checkbox("Inverter Y", &e.invertY);
        ch2 |= ImGui::SliderFloat("Largura", &e.w, 0.05f, 1.f); ch2 |= ImGui::SliderFloat("Altura", &e.h, 0.05f, 1.f);
        ImGui::TextColored(P.muted, "Ativa quando o mouse está capturado.");
        break;
    case ElemType::MoveArea:
        bind("Tecla (segurar)", e.key, true);
        ch2 |= ImGui::SliderFloat("Largura", &e.w, 0.05f, 1.f); ch2 |= ImGui::SliderFloat("Altura", &e.h, 0.05f, 1.f);
        ImGui::TextColored(P.muted, "Segure a tecla e mova o mouse para arrastar dentro da área.");
        break;
    case ElemType::Swipe:
        bind("Tecla", e.key, true);
        ch2 |= ImGui::SliderInt("Duração (ms)", &e.swipeMs, 30, 800);
        ch2 |= ImGui::SliderFloat("Destino X", &e.x2, 0.f, 1.f); ch2 |= ImGui::SliderFloat("Destino Y", &e.y2, 0.f, 1.f);
        ch2 |= ImGui::SliderFloat("Raio", &e.r, 0.01f, 0.25f, "%.3f");
        break;
    case ElemType::Aim:
        bind("Tecla", e.key, true);
        ch2 |= ImGui::Checkbox("Segurar (desmarcado = alternar com um toque)", &e.holdMode);
        ch2 |= ImGui::SliderFloat("Raio", &e.r, 0.01f, 0.25f, "%.3f");
        break;
    default:
        bind("Tecla", e.key, true);
        ch2 |= ImGui::SliderFloat("Raio", &e.r, 0.01f, 0.25f, "%.3f");
        if (e.type == ElemType::Tap) ImGui::TextColored(P.muted, "Toque rápido (40 ms) a cada pressionamento.");
        break;
    }
    ImGui::SeparatorText("Posição");
    ch2 |= ImGui::SliderFloat("X", &e.x, 0.f, 1.f, "%.3f");
    ch2 |= ImGui::SliderFloat("Y", &e.y, 0.f, 1.f, "%.3f");
    ImGui::Spacing();
    if (ImGui::Button("Duplicar controle")) {
        Element copy = e;
        copy.x = std::min(1.f, copy.x + 0.03f);
        capturing_ = nullptr;
        prof->elements.push_back(copy);
        selElem_ = (int)prof->elements.size() - 1;
        ch2 = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(P.danger.x, P.danger.y, P.danger.z, 0.35f));
    if (ImGui::Button("Remover")) {
        capturing_ = nullptr;
        prof->elements.erase(prof->elements.begin() + selElem_);
        selElem_ = -1;
        ch2 = true;
    }
    ImGui::PopStyleColor();
    if (ch2) dirty = true;
    ImGui::EndChild();
}
}

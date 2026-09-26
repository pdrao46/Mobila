#include "App.h"
#include "Theme.h"
#include "../core/Clock.h"
#include "../core/Log.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace mob {
static void fmt(char* b, size_t n, const char* f, double v) { std::snprintf(b, n, f, v); }

// ---------------------------------------------------------------- estrutura
void App::drawSplash() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    float t = (float)ImGui::GetTime();
    float a = std::min(1.f, t / 0.4f);
    float s = 150 * fontScaleBuilt_;
    ImVec2 c(vp->Pos.x + vp->Size.x / 2, vp->Pos.y + vp->Size.y / 2 - 40 * fontScaleBuilt_);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawLogo(dl, ImVec2(c.x - s / 2, c.y - s / 2 - 10 * (1 - a)), s, t);
    ImGui::PushFont(fonts().hero);
    ImVec2 ts = ImGui::CalcTextSize(MOB_APP_NAME);
    dl->AddText(ImVec2(c.x - ts.x / 2, c.y + s / 2 + 18 * fontScaleBuilt_), col(pal().text, a), MOB_APP_NAME);
    ImGui::PopFont();
    ImGui::PushFont(fonts().bold);
    ImVec2 ss = ImGui::CalcTextSize(MOB_APP_SUBTITLE);
    dl->AddText(ImVec2(c.x - ss.x / 2, c.y + s / 2 + 18 * fontScaleBuilt_ + ts.y + 4), col(pal().accent, a), MOB_APP_SUBTITLE);
    ImGui::PopFont();
    // barra de progresso indeterminada
    float bw = 220 * fontScaleBuilt_, by = c.y + s / 2 + 110 * fontScaleBuilt_;
    dl->AddRectFilled(ImVec2(c.x - bw / 2, by), ImVec2(c.x + bw / 2, by + 3), col(pal().border), 2);
    float p = std::fmod(t * 0.9f, 1.f);
    dl->AddRectFilled(ImVec2(c.x - bw / 2 + bw * p * 0.7f, by), ImVec2(c.x - bw / 2 + bw * (p * 0.7f + 0.3f), by + 3), col(pal().accent), 2);
    ImGui::End();
}

void App::drawSidebar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = 230 * fontScaleBuilt_;
    const Palette& P = pal();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(w, vp->Size.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, P.panel);
    ImGui::Begin("##side", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float ls = 38 * fontScaleBuilt_;
    drawLogo(dl, p, ls);
    ImGui::SetCursorScreenPos(ImVec2(p.x + ls + 10, p.y - 2));
    ImGui::PushFont(fonts().title);
    ImGui::TextUnformatted(MOB_APP_NAME);
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + ls + 10, ImGui::GetCursorScreenPos().y - 6));
    ImGui::TextColored(P.muted, "%s", MOB_APP_SUBTITLE);
    ImGui::Dummy(ImVec2(0, 16 * fontScaleBuilt_));

    struct Item { View v; const char* ic; const char* label; };
    Item items[] = {{View::Home, icon::home(), "Início"},
                    {View::Play, icon::play(), "Jogar"},
                    {View::Editor, icon::keyboard(), "Mapeamento"},
                    {View::Performance, icon::perf(), "Performance"},
                    {View::Benchmark, icon::bench(), "Benchmark"},
                    {View::Settings, icon::settings(), "Configurações"},
                    {View::Diagnostics, icon::logs(), "Diagnóstico"}};
    for (auto& it : items) {
        bool sel = view_ == it.v;
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "%s%s", it.ic, it.label);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float h = 38 * fontScaleBuilt_;
        if (ImGui::InvisibleButton(it.label, ImVec2(-1, h))) view_ = it.v;
        bool hov = ImGui::IsItemHovered();
        ImVec2 end(pos.x + ImGui::GetItemRectSize().x, pos.y + h);
        if (sel) {
            dl->AddRectFilled(pos, end, col(P.accent, 0.12f), 8);
            dl->AddRectFilled(pos, ImVec2(pos.x + 3, end.y), col(P.accent), 2);
        } else if (hov) dl->AddRectFilled(pos, end, col(P.text, 0.05f), 8);
        dl->AddText(ImVec2(pos.x + 14, pos.y + (h - ImGui::GetTextLineHeight()) / 2), col(sel ? P.accent : P.text, sel ? 1 : 0.85f), lbl);
    }

    // rodapé: estado da conexão
    float footY = vp->Size.y - 96 * fontScaleBuilt_;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 10, footY));
    ImGui::Separator();
    auto st = session_->state();
    statusDot(st == SessionState::Streaming, st == SessionState::Connecting);
    ImGui::TextUnformatted(st == SessionState::Streaming ? "Transmitindo" : st == SessionState::Connecting ? "Conectando..." :
                           st == SessionState::Error ? "Erro / reconectando" : "Ocioso");
    ImGui::TextColored(P.muted, "v%s  •  %s", MOB_VERSION, videoModeName(cfg_.video.mode));
    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawTopbar() {
    const Palette& P = pal();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    bool captured = mapper_->mouseCaptured();
    float h = 48 * fontScaleBuilt_;
    // auto-oculta em tela cheia/captura; reaparece com o mouse no topo
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    bool show = !captured && (!fullscreen_ || my < h * 1.5f);
    if (!show) return;
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(P.panel.x, P.panel.y, P.panel.z, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * fontScaleBuilt_, 8 * fontScaleBuilt_));
    ImGui::Begin("##top", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("‹  Voltar")) { setMouseCapture(false); view_ = View::Home; }
    ImGui::SameLine();
    drawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ImGui::GetFrameHeight());
    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 0));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts().bold);
    ImGui::TextUnformatted(MOB_APP_NAME);
    ImGui::PopFont();
    ImGui::SameLine();
    auto st = session_->state();
    statusDot(st == SessionState::Streaming, st == SessionState::Connecting);
    std::string dn = session_->deviceName();
    ImGui::TextColored(P.muted, "%s  •  %dx%d  •  %.0f FPS  •  %s", dn.empty() ? "—" : dn.c_str(), snap_.width,
                       snap_.height, snap_.fpsRendered, videoModeName(cfg_.video.mode));
    float bw = 150 * fontScaleBuilt_;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw * 3 - 40 * fontScaleBuilt_);
    if (ImGui::Button(mappingEnabled_ ? "Mapeamento: ON" : "Mapeamento: OFF", ImVec2(bw, 0))) {
        mappingEnabled_ = !mappingEnabled_;
        mapper_->setEnabled(mappingEnabled_);
    }
    ImGui::SameLine();
    if (ImGui::Button(showControls_ ? "Ocultar controles" : "Mostrar controles", ImVec2(bw, 0))) showControls_ = !showControls_;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(P.accent.x, P.accent.y, P.accent.z, 0.25f));
    char cap[64];
    std::snprintf(cap, sizeof cap, "%sCAPTURAR MOUSE", icon::mouse());
    if (ImGui::Button(cap, ImVec2(bw, 0))) setMouseCapture(true);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Libera com %s. Atalho de captura: %s", cfg_.hotkeys.releaseMouse.c_str(),
                                                  cfg_.hotkeys.captureMouse.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- início
void App::viewHome() {
    const Palette& P = pal();
    float s = fontScaleBuilt_;
    std::vector<AdbDevice> devs;
    DeviceInfo info;
    {
        std::lock_guard<std::mutex> lk(devMx_);
        devs = devices_;
        info = info_;
    }
    auto st = session_->state();
    bool connected = st == SessionState::Streaming;
    const AdbDevice* ready = nullptr;
    const AdbDevice* unauth = nullptr;
    for (auto& d : devs) {
        if (d.state == "device" && !ready) ready = &d;
        if (d.state == "unauthorized") unauth = &d;
    }

    // HERO
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float heroH = 250 * s, W = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + W, p0.y + heroH), col(P.accent, 0.10f), col(P.accent2, 0.12f),
                                col(P.accent2, 0.03f), col(P.accent, 0.02f));
    dl->AddRect(p0, ImVec2(p0.x + W, p0.y + heroH), col(P.border), 14);
    float ls = 120 * s;
    drawLogo(dl, ImVec2(p0.x + 34 * s, p0.y + (heroH - ls) / 2), ls, (float)ImGui::GetTime());
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 34 * s + ls + 30 * s, p0.y + 36 * s));
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().hero);
    ImGui::TextUnformatted(MOB_APP_NAME);
    ImGui::PopFont();
    ImGui::PushFont(fonts().bold);
    ImGui::TextColored(P.accent, "%s", MOB_APP_SUBTITLE);
    ImGui::PopFont();
    ImGui::TextColored(P.muted, "Seu celular. Seu PC. Sua conexão.");
    ImGui::Dummy(ImVec2(0, 8 * s));
    if (connected) { statusDot(true); ImGui::TextColored(P.success, "Dispositivo conectado"); }
    else if (st == SessionState::Connecting) { statusDot(false, true); ImGui::TextColored(P.warn, "Conectando..."); }
    else if (ready) { statusDot(false, true); ImGui::Text("Dispositivo detectado: %s", ready->model.empty() ? ready->serial.c_str() : ready->model.c_str()); }
    else if (unauth) { statusDot(false, true); ImGui::TextColored(P.warn, "Autorize a depuração USB na tela do celular"); }
    else { statusDot(false); ImGui::TextColored(P.muted, "Nenhum dispositivo conectado"); }
    ImGui::Dummy(ImVec2(0, 6 * s));
    if (!connected && st != SessionState::Connecting) {
        ImGui::BeginDisabled(!ready || !adbReady_);
        char lbl[64];
        std::snprintf(lbl, sizeof lbl, "%sCONECTAR CELULAR", icon::usb());
        if (primaryButton(lbl, ImVec2(250 * s, 46 * s))) connect();
        ImGui::EndDisabled();
    } else {
        if (primaryButton("ABRIR TELA DO JOGO", ImVec2(230 * s, 46 * s))) view_ = View::Play;
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8 * s);
        if (ImGui::Button("Desconectar", ImVec2(130 * s, 30 * s))) disconnect();
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + heroH + 18 * s));

    if (st == SessionState::Error) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(P.danger.x, P.danger.y, P.danger.z, 0.10f));
        if (beginCard("##err")) {
            ImGui::TextColored(P.danger, "Falha na sessão");
            ImGui::TextWrapped("%s", session_->error().c_str());
            if (wantConnected_ && cfg_.autoReconnect) ImGui::TextColored(P.muted, "Reconexão automática ativa...");
        }
        endCard();
        ImGui::PopStyleColor();
    }
    std::string boot;
    { std::lock_guard<std::mutex> lk(devMx_); boot = bootStatus_; }
    if (!boot.empty()) ImGui::TextColored(P.warn, "%s", boot.c_str());
    else if (!adbReady_) ImGui::TextColored(P.warn, "Iniciando ADB...");

    // CARDS DO DISPOSITIVO
    sectionTitle("Dispositivo", "Detectado automaticamente via ADB (USB)");
    char buf[128];
    float cw = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
    auto tile = [&](const char* id, const char* label, const char* value, ImVec4* c = nullptr, const char* hint = nullptr) {
        if (beginCard(id, ImVec2(cw, 104 * s))) metricTile(label, value, hint, c);
        endCard();
    };
    bool have = !info.serial.empty() && (connected || st == SessionState::Connecting || ready);
    std::string model = have ? info.manufacturer + " " + info.model : "—";
    tile("##m", "MODELO", model.c_str());
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, have ? "Android %s (API %s)" : "—", info.androidVersion.c_str(), info.sdk.c_str());
    tile("##a", "SISTEMA", buf);
    ImGui::SameLine();
    if (have) std::snprintf(buf, sizeof buf, "%dx%d", info.width, info.height); else std::snprintf(buf, sizeof buf, "—");
    char hint[96];
    if (connected) std::snprintf(hint, sizeof hint, "stream %dx%d", snap_.width, snap_.height);
    else if (have) std::snprintf(hint, sizeof hint, "%d dpi", info.density);
    else std::snprintf(hint, sizeof hint, " ");
    tile("##r", "RESOLUÇÃO", buf, nullptr, hint);
    ImGui::SameLine();
    if (connected) std::snprintf(buf, sizeof buf, "%.0f FPS", snap_.fpsRendered); else std::snprintf(buf, sizeof buf, have ? "%.0f Hz" : "—", info.refreshRate);
    std::snprintf(hint, sizeof hint, have ? "tela %.0f Hz" : " ", info.refreshRate);
    tile("##f", "FPS", buf, nullptr, hint);

    const AdbDevice* cur = nullptr;
    for (auto& d : devs) if (d.serial == (targetSerial_.empty() && ready ? ready->serial : targetSerial_)) cur = &d;
    std::snprintf(buf, sizeof buf, "%s", cur ? (cur->isUsb() ? "USB" : "Rede (não recomendado)") : "—");
    ImVec4 okc = P.success;
    tile("##u", "CONEXÃO", buf, cur && cur->isUsb() ? &okc : nullptr, cur ? cur->usbPath.c_str() : " ");
    ImGui::SameLine();
    std::snprintf(buf, sizeof buf, "%s", !adbReady_ ? "Indisponível" : cur ? (cur->state == "device" ? "Autorizado" : cur->state.c_str()) : "Aguardando");
    tile("##adb", "STATUS ADB", buf, cur && cur->state == "device" ? &okc : nullptr, adbVersion_.c_str());
    ImGui::SameLine();
    double pipeline = snap_.stage[ST_DECODE].avgMs + snap_.stage[ST_UPLOAD].avgMs + snap_.stage[ST_PRESENT].avgMs +
                      snap_.stage[ST_USB].avgMs;
    if (lastE2E_ > 0) std::snprintf(buf, sizeof buf, "%.0f ms", lastE2E_ + snap_.stage[ST_UPLOAD].avgMs + snap_.stage[ST_PRESENT].avgMs);
    else if (connected) std::snprintf(buf, sizeof buf, "PC: %.1f ms", pipeline);
    else std::snprintf(buf, sizeof buf, "—");
    tile("##lat", "LATÊNCIA ESTIMADA", buf, nullptr, lastE2E_ > 0 ? "medida ponta a ponta" : "rode o teste em Performance");
    ImGui::SameLine();
    if (info.batteryTempC > 0 && have) std::snprintf(buf, sizeof buf, "%.1f °C", info.batteryTempC); else std::snprintf(buf, sizeof buf, "—");
    tile("##t", "TEMPERATURA", buf, nullptr, "bateria do celular");

    ImGui::Spacing();
    // MODO + PERFIL
    sectionTitle("Modo de vídeo", "Aplicado na próxima conexão (ou reinicie o stream)");
    VideoMode modes[] = {VideoMode::UltraLowLatency, VideoMode::Balanced, VideoMode::Quality};
    const char* desc[] = {"Menor latência possível. Buffer de 1 frame, sem VSync, 1280p, H.264 prioridade tempo real.",
                          "Equilíbrio entre latência, nitidez e estabilidade. 1600p, 12 Mbps.",
                          "Prioriza qualidade visual. Resolução nativa, 20 Mbps, VSync ligado."};
    float mw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int i = 0; i < 3; ++i) {
        bool sel = cfg_.video.mode == modes[i];
        if (sel) ImGui::PushStyleColor(ImGuiCol_Border, P.accent);
        char id[16];
        std::snprintf(id, sizeof id, "##mode%d", i);
        if (beginCard(id, ImVec2(mw, 110 * s))) {
            ImGui::PushFont(fonts().bold);
            ImGui::TextColored(sel ? P.accent : P.text, "%s%s", i == 0 ? icon::bolt() : "", videoModeName(modes[i]));
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, P.muted);
            ImGui::TextWrapped("%s", desc[i]);
            ImGui::PopStyleColor();
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                applyVideoPreset(cfg_.video, modes[i]);
                saveSettings();
                if (connected) restartWith(cfg_.video);
            }
        }
        endCard();
        if (sel) ImGui::PopStyleColor();
        if (i < 2) ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(320 * s);
    if (ImGui::BeginCombo("Perfil de mapeamento", cfg_.activeProfile.c_str())) {
        for (auto& p : profiles_->all())
            if (ImGui::Selectable(p.name.c_str(), p.name == cfg_.activeProfile)) {
                cfg_.activeProfile = p.name;
                mapper_->setProfile(activeProfile());
                saveSettings();
            }
        ImGui::EndCombo();
    }
    if (!info.videoEncoders.empty() && have) {
        ImGui::TextColored(P.muted, "Encoders de vídeo do aparelho:");
        for (auto& e : info.videoEncoders) ImGui::BulletText("%s", e.c_str());
    }
}

// ---------------------------------------------------------------- jogar
void App::viewPlay() {
    const Palette& P = pal();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    auto st = session_->state();
    float top = (fullscreen_ || mapper_->mouseCaptured()) ? 0 : 48 * fontScaleBuilt_;
    float aw = vp->Size.x, ah = vp->Size.y - top;
    int vw = session_->videoWidth(), vh = session_->videoHeight();
    if (st != SessionState::Streaming || !tex_ || !vw) {
        const char* msg = st == SessionState::Connecting ? "Iniciando transmissão..." :
                          st == SessionState::Error ? "Conexão perdida — tentando recuperar" : "Nenhuma transmissão ativa";
        ImGui::PushFont(fonts().title);
        ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(ImVec2(vp->Pos.x + (aw - ts.x) / 2, vp->Pos.y + top + ah / 2 - ts.y), col(P.muted), msg);
        ImGui::PopFont();
        if (st == SessionState::Error) {
            std::string e = session_->error();
            ImVec2 es = ImGui::CalcTextSize(e.c_str());
            ImGui::GetWindowDrawList()->AddText(ImVec2(vp->Pos.x + (aw - es.x) / 2, vp->Pos.y + top + ah / 2 + 10), col(P.danger), e.c_str());
        }
        return;
    }
    int rot = cfg_.video.rotation;
    float rw = (rot == 90 || rot == 270) ? (float)vh : (float)vw;
    float rh = (rot == 90 || rot == 270) ? (float)vw : (float)vh;
    float scale = std::min(aw / rw, ah / rh) * std::clamp(cfg_.video.renderScale, 0.25f, 1.0f);
    if (cfg_.video.integerScaling && scale >= 1.f) scale = std::floor(scale);
    float dw = rw * scale, dh = rh * scale;
    videoRect_ = {(int)(vp->Pos.x + (aw - dw) / 2), (int)(vp->Pos.y + top + (ah - dh) / 2), (int)dw, (int)dh};
    videoVisible_ = true;
    if (showControls_) drawOverlayControls(ImGui::GetForegroundDrawList(), ImVec2((float)videoRect_.x, (float)videoRect_.y),
                                           ImVec2(dw, dh), false);
    if (mapper_->mouseCaptured()) {
        const char* m = "Mouse capturado — ";
        char b[128];
        std::snprintf(b, sizeof b, "%s%s para liberar", m, cfg_.hotkeys.releaseMouse.c_str());
        ImGui::GetForegroundDrawList()->AddText(ImVec2(vp->Pos.x + 12, vp->Pos.y + vp->Size.y - 26 * fontScaleBuilt_), col(P.muted, 0.7f), b);
    }
}

void App::drawPerfOverlay() {
    const Palette& P = pal();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float top = (fullscreen_ || mapper_->mouseCaptured()) ? 8 : 56 * fontScaleBuilt_;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 10, vp->Pos.y + top), 0, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##perfov", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushFont(fonts().mono);
    ImGui::TextColored(P.accent, "MOBILADOR PERFORMANCE");
    ImGui::Text("FPS  rx %5.1f  draw %5.1f", snap_.fpsReceived, snap_.fpsRendered);
    ImGui::Text("frame %5.2f ms  σ %4.2f  p99 %5.2f", snap_.frameTimeAvgMs, snap_.frameTimeStdMs, snap_.frameTimeP99Ms);
    ImGui::Text("USB %4.2f  dec %4.2f  up %4.2f  pres %4.2f", snap_.stage[ST_USB].avgMs, snap_.stage[ST_DECODE].avgMs,
                snap_.stage[ST_UPLOAD].avgMs, snap_.stage[ST_PRESENT].avgMs);
    ImGui::Text("input %4.2f ms  %4.0f ev/s  drop %llu", snap_.stage[ST_INPUT].avgMs, snap_.inputRate,
                (unsigned long long)snap_.droppedTotal);
    ImGui::Text("%.1f Mbps  %s  CPU %4.1f%%  RAM %.0f MB", snap_.bitrateMbps, snap_.hwDecode ? "GPU-dec" : "CPU-dec",
                hostUsage_.cpuPercent, hostUsage_.ramMB);
    if (lastE2E_ > 0) ImGui::TextColored(P.success, "ponta a ponta %.1f ms", lastE2E_);
    ImGui::PopFont();
    ImGui::End();
}

// ---------------------------------------------------------------- performance
void App::drawLatencyAnalyzer(bool compact) {
    const Palette& P = pal();
    struct Row { const char* name; double ms; bool measured; const char* how; };
    double usb = snap_.stage[ST_USB].avgMs, dec = snap_.stage[ST_DECODE].avgMs, up = snap_.stage[ST_UPLOAD].avgMs + snap_.stage[ST_QUEUE].avgMs,
           pres = snap_.stage[ST_PRESENT].avgMs, in = snap_.stage[ST_INPUT].avgMs;
    // Parte do dispositivo = medida ponta a ponta menos o que é medido no PC.
    double device = lastE2E_ > 0 ? std::max(0.0, lastE2E_ - usb - dec - in) : -1;
    Row rows[] = {
        {"CAPTURE + ENCODE LATENCY", device, device >= 0, "ponta a ponta − (USB + decode + input); inclui injeção e render do jogo"},
        {"USB LATENCY", usb, true, "tempo de chegada do pacote (cabeçalho → último byte)"},
        {"PIPELINE QUEUE (jitter)", snap_.stage[ST_JITTER].avgMs, true, "atraso acima da melhor linha base (fila no encoder/USB)"},
        {"DECODE LATENCY", dec, true, "envio do pacote → frame pronto (inclui cópia GPU→RAM)"},
        {"RENDER LATENCY", up + pres, true, "fila + upload da textura + present"},
        {"INPUT LATENCY", in, true, "evento do SO → mensagem enviada ao Android (resolução 1 ms)"},
    };
    double total = (device >= 0 ? device : 0) + usb + dec + up + pres + in;
    double mx = 1;
    for (auto& r : rows) mx = std::max(mx, r.ms);
    if (ImGui::BeginTable("##lat", compact ? 2 : 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Etapa", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        if (!compact) ImGui::TableSetupColumn("Como é medido", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        for (auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushFont(fonts().mono);
            ImGui::TextUnformatted(r.name);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            if (!r.measured) { ImGui::TextColored(P.muted, "execute o teste ponta a ponta"); }
            else {
                char b[32];
                std::snprintf(b, sizeof b, "%.2f ms", r.ms);
                float frac = (float)(r.ms / mx);
                ImVec4 c = r.ms == mx && mx > 2 ? P.warn : P.accent;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, c);
                ImGui::ProgressBar(frac, ImVec2(-1, 0), b);
                ImGui::PopStyleColor();
            }
            if (!compact) { ImGui::TableNextColumn(); ImGui::TextColored(P.muted, "%s", r.how); }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushFont(fonts().bold);
        ImGui::TextColored(P.accent, "TOTAL ESTIMATED LATENCY");
        ImGui::PopFont();
        ImGui::TableNextColumn();
        ImGui::PushFont(fonts().bold);
        if (device >= 0) ImGui::Text("%.1f ms", total);
        else ImGui::TextColored(P.muted, "PC: %.1f ms (+ dispositivo: não medido)", total);
        ImGui::PopFont();
        if (!compact) { ImGui::TableNextColumn(); ImGui::TextColored(P.muted, "não inclui scan-out do monitor"); }
        ImGui::EndTable();
    }
}

void App::viewPerformance() {
    const Palette& P = pal();
    float s = fontScaleBuilt_;
    sectionTitle("MOBILADOR PERFORMANCE", "Métricas reais coletadas a cada segundo. Nada aqui é simulado.");
    char v[64], h[96];
    float cw = (ImGui::GetContentRegionAvail().x - 5 * ImGui::GetStyle().ItemSpacing.x) / 6;
    auto tile = [&](const char* id, const char* label, const char* value, const char* hint) {
        if (beginCard(id, ImVec2(cw, 104 * s))) metricTile(label, value, hint);
        endCard();
    };
    fmt(v, sizeof v, "%.1f", snap_.fpsReceived); tile("##p1", "FPS RECEBIDO", v, "do celular");
    ImGui::SameLine(); fmt(v, sizeof v, "%.1f", snap_.fpsRendered); tile("##p2", "FPS RENDERIZADO", v, "na janela");
    ImGui::SameLine(); std::snprintf(v, sizeof v, "%llu", (unsigned long long)snap_.droppedTotal);
    fmt(h, sizeof h, "%.1f/s agora", snap_.droppedPerSec); tile("##p3", "DESCARTADOS", v, h);
    ImGui::SameLine(); fmt(v, sizeof v, "%.1f Mbps", snap_.bitrateMbps); tile("##p4", "BITRATE", v, "medido");
    ImGui::SameLine(); std::snprintf(v, sizeof v, "%dx%d", snap_.width, snap_.height); tile("##p5", "RESOLUÇÃO", v, snap_.hwDecode ? "decode GPU" : "decode CPU");
    ImGui::SameLine(); std::snprintf(v, sizeof v, "%d / %d", snap_.pendingFrames, cfg_.video.frameQueue); tile("##p6", "BUFFER", v, "frames pendentes / máx");

    fmt(v, sizeof v, "%.1f %%", hostUsage_.cpuPercent); tile("##h1", "CPU", v, "processo Mobilador");
    ImGui::SameLine(); if (hostUsage_.gpuPercent >= 0) fmt(v, sizeof v, "%.1f %%", hostUsage_.gpuPercent); else std::snprintf(v, sizeof v, "n/d");
    tile("##h2", "GPU", v, "engine mais ocupado");
    ImGui::SameLine(); fmt(v, sizeof v, "%.0f MB", hostUsage_.ramMB); tile("##h3", "RAM", v, "working set");
    ImGui::SameLine();
    float temp;
    { std::lock_guard<std::mutex> lk(devMx_); temp = info_.batteryTempC; }
    if (temp > 0) fmt(v, sizeof v, "%.1f °C", temp); else std::snprintf(v, sizeof v, "n/d");
    tile("##h4", "TEMPERATURA", v, "bateria do celular");
    ImGui::SameLine();
    auto st = session_->state();
    tile("##h5", "USB / ADB", st == SessionState::Streaming ? "Ativo" : st == SessionState::Connecting ? "Conectando" : "Inativo",
         session_->serial().c_str());
    ImGui::SameLine(); fmt(v, sizeof v, "%.0f ev/s", snap_.inputRate); fmt(h, sizeof h, "%.2f ms médio", snap_.stage[ST_INPUT].avgMs);
    tile("##h6", "TAXA DE INPUT", v, h);

    ImGui::Spacing();
    if (beginCard("##ft")) {
        ImGui::PushFont(fonts().bold);
        ImGui::Text("Frame time (ms) — média %.2f  •  desvio %.2f  •  p99 %.2f", snap_.frameTimeAvgMs, snap_.frameTimeStdMs, snap_.frameTimeP99Ms);
        ImGui::PopFont();
        auto ft = metrics_.frameTimes();
        float mx = 0;
        for (float f : ft) mx = std::max(mx, f);
        ImGui::PlotLines("##ftp", ft.data(), (int)ft.size(), 0, nullptr, 0, std::max(20.f, mx * 1.1f), ImVec2(-1, 90 * s));
        ImGui::TextColored(P.muted, "Picos = stutter. Com a tela do celular parada o servidor não envia frames (intervalos longos são normais).");
    }
    endCard();

    ImGui::Spacing();
    if (beginCard("##an")) {
        sectionTitle("LATENCY ANALYZER", "Separação por etapa para localizar o gargalo");
        drawLatencyAnalyzer(false);
        ImGui::Separator();
        ImGui::PushFont(fonts().bold);
        ImGui::TextUnformatted("Teste de latência ponta a ponta (medição real)");
        ImGui::PopFont();
        ImGui::TextColored(P.muted,
            "Injeta toques e detecta no vídeo decodificado o indicador de toque do Android. Ative \"Mostrar toques\"\n"
            "(Configurações → Conexão, ou Opções do desenvolvedor) e escolha um ponto de tela estática.");
        ImGui::SetNextItemWidth(160 * s); ImGui::SliderFloat("Ponto X", &probeX_, 0.05f, 0.95f, "%.2f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(160 * s); ImGui::SliderFloat("Ponto Y", &probeY_, 0.05f, 0.95f, "%.2f");
        ImGui::BeginDisabled(st != SessionState::Streaming || bench_.running());
        if (!tester_.running()) { if (ImGui::Button("Medir (10 amostras)")) tester_.start(session_.get(), 10, probeX_, probeY_); }
        else if (ImGui::Button("Cancelar")) tester_.cancel();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s", tester_.target() ? tester_.status().c_str() : "");
        if (!tester_.results().empty())
            ImGui::TextColored(P.success, "input→decode: média %.1f ms  •  mín %.1f  •  máx %.1f", tester_.avg(), tester_.minv(), tester_.maxv());
        if (tester_.timeouts() > 0 && tester_.results().empty())
            ImGui::TextColored(P.warn, "Sem detecção: confirme \"Mostrar toques\" e use uma área sem animação.");
    }
    endCard();
}

// ---------------------------------------------------------------- benchmark
static bool videoEditor(const char* id, VideoSettings& v, float s) {
    bool ch = false;
    ImGui::PushID(id);
    const char* modes[] = {"ULTRA LOW LATENCY", "BALANCED", "QUALITY", "CUSTOM"};
    int m = (int)v.mode;
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo("Preset", &m, modes, 4)) { applyVideoPreset(v, (VideoMode)m); ch = true; }
    auto custom = [&] { v.mode = VideoMode::Custom; ch = true; };
    const char* sizes[] = {"Nativa", "720", "1024", "1280", "1600", "1920"};
    int sv[] = {0, 720, 1024, 1280, 1600, 1920};
    int si = 0;
    for (int i = 0; i < 6; ++i) if (sv[i] == v.maxSize) si = i;
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo("Resolução máx.", &si, sizes, 6)) { v.maxSize = sv[si]; custom(); }
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::SliderInt("Bitrate (Mbps)", &v.bitrateMbps, 2, 50)) custom();
    const char* fps[] = {"Livre (o que o celular entregar)", "30", "60", "90", "120"};
    int fv[] = {0, 30, 60, 90, 120}, fi = 0;
    for (int i = 0; i < 5; ++i) if (fv[i] == v.maxFps) fi = i;
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo("Limite de FPS", &fi, fps, 5)) { v.maxFps = fv[fi]; custom(); }
    int c = (int)v.codec;
    const char* codecs[] = {"H.264", "H.265", "AV1"};
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo("Codec", &c, codecs, 3)) { v.codec = (Codec)c; custom(); }
    int d = (int)v.decoder;
    const char* decs[] = {"Automático (GPU se disponível)", "Hardware (D3D11VA)", "Software (CPU)"};
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::Combo("Decodificador", &d, decs, 3)) { v.decoder = (DecoderPref)d; custom(); }
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::SliderInt("Buffer (frames)", &v.frameQueue, 1, 3)) custom();
    if (ImGui::Checkbox("VSync", &v.vsync)) custom();
    ImGui::PopID();
    return ch;
}

void App::viewBenchmark() {
    const Palette& P = pal();
    float s = fontScaleBuilt_;
    sectionTitle("MOBILADOR BENCHMARK",
                 "Testa cada configuração com a mesma metodologia e mostra todos os dados. A escolha é sua.");
    bool streaming = session_->state() == SessionState::Streaming || bench_.running();
    float cw = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (size_t i = 0; i < bench_.configs.size(); ++i) {
        auto& c = bench_.configs[i];
        char id[32];
        std::snprintf(id, sizeof id, "##bc%zu", i);
        if (beginCard(id, ImVec2(cw, 0))) {
            ImGui::PushFont(fonts().bold);
            ImGui::Checkbox(c.name.c_str(), &c.enabled);
            ImGui::PopFont();
            ImGui::BeginDisabled(bench_.running());
            videoEditor(id, c.video, s * 0.9f);
            ImGui::EndDisabled();
        }
        endCard();
        if (i + 1 < bench_.configs.size()) ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(120 * s); ImGui::SliderInt("Aquecimento (s)", &bench_.warmupSec, 1, 10);
    ImGui::SameLine(); ImGui::SetNextItemWidth(120 * s); ImGui::SliderInt("Medição (s)", &bench_.measureSec, 5, 60);
    ImGui::SameLine(); ImGui::Checkbox("Medir latência ponta a ponta", &bench_.measureE2E);
    ImGui::TextColored(P.muted, "Durante o teste mantenha o jogo em movimento (o servidor só envia frames quando a tela muda).");
    if (!bench_.running()) {
        ImGui::BeginDisabled(!streaming);
        if (primaryButton("INICIAR BENCHMARK", ImVec2(230 * s, 42 * s))) { tester_.cancel(); bench_.start(session_.get()); }
        ImGui::EndDisabled();
        if (!streaming) { ImGui::SameLine(); ImGui::TextColored(P.warn, "Conecte o celular primeiro."); }
    } else {
        if (primaryButton("CANCELAR", ImVec2(160 * s, 42 * s), true)) { bench_.cancel(); restartWith(cfg_.video); }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", bench_.status().c_str());
        ImGui::ProgressBar(bench_.progress(), ImVec2(300 * s, 0));
        ImGui::EndGroup();
    }
    auto& R = bench_.results();
    if (R.empty()) return;
    ImGui::Spacing();
    sectionTitle("Resultados", "Melhor valor de cada linha destacado — apenas para leitura, nada é aplicado automaticamente");
    struct Metric { const char* name; std::function<double(const BenchResult&)> get; bool lowerBetter; const char* unit; };
    std::vector<Metric> ms = {
        {"FPS renderizado (média)", [](auto& r) { return r.fpsRender; }, false, ""},
        {"FPS recebido (média)", [](auto& r) { return r.fpsRecv; }, false, ""},
        {"FPS mínimo (1 s)", [](auto& r) { return r.fpsMin; }, false, ""},
        {"Estabilidade FPS (desvio)", [](auto& r) { return r.fpsStd; }, true, ""},
        {"Frame time médio", [](auto& r) { return r.ftAvg; }, true, " ms"},
        {"Frame time desvio", [](auto& r) { return r.ftStd; }, true, " ms"},
        {"Frame time p99 (pior)", [](auto& r) { return r.ftP99; }, true, " ms"},
        {"Frames perdidos (total)", [](auto& r) { return r.dropped; }, true, ""},
        {"Bitrate", [](auto& r) { return r.bitrate; }, true, " Mbps"},
        {"USB", [](auto& r) { return r.usb; }, true, " ms"},
        {"Decode", [](auto& r) { return r.decode; }, true, " ms"},
        {"Upload + present", [](auto& r) { return r.upload + r.present; }, true, " ms"},
        {"Fila no pipeline (jitter)", [](auto& r) { return r.jitter; }, true, " ms"},
        {"Input (host)", [](auto& r) { return r.input; }, true, " ms"},
        {"Latência ponta a ponta (média)", [](auto& r) { return r.e2eSamples ? r.e2eAvg : -1; }, true, " ms"},
        {"Latência ponta a ponta (máx)", [](auto& r) { return r.e2eSamples ? r.e2eMax : -1; }, true, " ms"},
        {"CPU", [](auto& r) { return r.cpu; }, true, " %"},
        {"GPU", [](auto& r) { return r.gpu; }, true, " %"},
        {"RAM (pico)", [](auto& r) { return r.ram; }, true, " MB"},
    };
    if (ImGui::BeginTable("##res", (int)R.size() + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Métrica");
        for (auto& r : R) ImGui::TableSetupColumn(r.name.c_str());
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(P.muted, "Configuração");
        for (auto& r : R) {
            ImGui::TableNextColumn();
            if (!r.error.empty()) ImGui::TextColored(P.danger, "%s", r.error.c_str());
            else ImGui::TextColored(P.muted, "%s\n%dx%d, decode %s", r.summary.c_str(), r.width, r.height, r.hw ? "GPU" : "CPU");
        }
        for (auto& m : ms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.name);
            double best = m.lowerBetter ? 1e18 : -1e18;
            for (auto& r : R) if (r.valid) { double x = m.get(r); if (x >= 0) best = m.lowerBetter ? std::min(best, x) : std::max(best, x); }
            for (auto& r : R) {
                ImGui::TableNextColumn();
                double x = m.get(r);
                if (!r.valid || x < 0) { ImGui::TextColored(P.muted, "—"); continue; }
                bool isBest = R.size() > 1 && std::fabs(x - best) < 1e-9;
                if (isBest) ImGui::TextColored(P.success, "%.2f%s", x, m.unit);
                else ImGui::Text("%.2f%s", x, m.unit);
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Exportar CSV")) {
        std::string path = appDataDir() + "/benchmark.csv";
        if (bench_.exportCsv(path)) LOGI("benchmark exportado: %s", path.c_str());
        std::string url = "file:///" + appDataDir();
        SDL_OpenURL(url.c_str());
    }
}

// ---------------------------------------------------------------- configurações
bool App::keyCaptureButton(const char* id, std::string& target, bool allowMouse) {
    bool active = capturing_ == &target;
    char lbl[96];
    std::snprintf(lbl, sizeof lbl, "%s##%s", active ? "Pressione uma tecla... (Esc cancela)" : (target.empty() ? "(nenhuma)" : target.c_str()), id);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(pal().accent.x, pal().accent.y, pal().accent.z, 0.35f));
    bool clicked = ImGui::Button(lbl, ImVec2(200 * fontScaleBuilt_, 0));
    if (active) ImGui::PopStyleColor();
    if (clicked) { capturing_ = &target; captureAllowMouse_ = allowMouse; }
    return clicked;
}

void App::viewSettings() {
    const Palette& P = pal();
    float s = fontScaleBuilt_;
    sectionTitle("Configurações", "Salvas automaticamente");
    if (ImGui::BeginTabBar("##cfg")) {
        if (ImGui::BeginTabItem("Vídeo")) {
            if (videoEditor("vid", cfg_.video, s)) saveSettings();
            ImGui::SeparatorText("Renderização");
            const char* rots[] = {"0°", "90°", "180°", "270°"};
            int r = cfg_.video.rotation / 90;
            ImGui::SetNextItemWidth(200 * s);
            if (ImGui::Combo("Rotação (GPU)", &r, rots, 4)) { cfg_.video.rotation = r * 90; saveSettings(); }
            ImGui::SetNextItemWidth(200 * s);
            if (ImGui::SliderFloat("Escala do vídeo", &cfg_.video.renderScale, 0.25f, 1.0f, "%.2f")) saveSettings();
            if (ImGui::Checkbox("Escala inteira (pixels nítidos)", &cfg_.video.integerScaling)) saveSettings();
            std::vector<std::string> encs;
            { std::lock_guard<std::mutex> lk(devMx_); encs = info_.videoEncoders; }
            ImGui::SetNextItemWidth(320 * s);
            if (ImGui::BeginCombo("Encoder do celular", cfg_.video.encoderName.empty() ? "Padrão do dispositivo" : cfg_.video.encoderName.c_str())) {
                if (ImGui::Selectable("Padrão do dispositivo", cfg_.video.encoderName.empty())) { cfg_.video.encoderName.clear(); saveSettings(); }
                for (auto& e : encs) {
                    std::string name = e.substr(e.find("  ") + 2);
                    name = name.substr(0, name.find("  "));
                    if (ImGui::Selectable(e.c_str(), name == cfg_.video.encoderName)) {
                        cfg_.video.encoderName = name;
                        cfg_.video.codec = e.rfind("h265", 0) == 0 ? Codec::H265 : e.rfind("av1", 0) == 0 ? Codec::AV1 : Codec::H264;
                        cfg_.video.mode = VideoMode::Custom;
                        saveSettings();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextColored(P.muted, "Prefira encoders [HW]. Encoders [SW] (c2.android.*) aumentam muito a latência.");
            if (session_->state() == SessionState::Streaming && ImGui::Button("Aplicar agora (reinicia o stream)")) restartWith(cfg_.video);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mouse")) {
            Profile* p = activeProfile();
            if (p) {
                ImGui::Text("Perfil: %s", p->name.c_str());
                bool ch = ImGui::SliderFloat("Sensibilidade global", &p->globalSensitivity, 0.1f, 5.0f, "%.2f");
                for (auto& e : p->elements) {
                    if (e.type != ElemType::Camera) continue;
                    ImGui::SeparatorText(e.label.c_str());
                    ch |= ImGui::SliderFloat("Sensibilidade", &e.sensitivity, 0.05f, 5.0f, "%.2f");
                    ch |= ImGui::SliderFloat("Multiplicador X", &e.multX, 0.1f, 3.0f, "%.2f");
                    ch |= ImGui::SliderFloat("Multiplicador Y", &e.multY, 0.1f, 3.0f, "%.2f");
                    ch |= ImGui::SliderFloat("Aceleração (0 = linear)", &e.accel, 0.0f, 2.0f, "%.2f");
                    ch |= ImGui::SliderFloat("Deadzone (contagens)", &e.deadzone, 0.0f, 5.0f, "%.1f");
                    ch |= ImGui::Checkbox("Inverter X", &e.invertX);
                    ImGui::SameLine();
                    ch |= ImGui::Checkbox("Inverter Y", &e.invertY);
                }
                if (ch) { profiles_->save(*p); mapper_->setProfile(p); }
            }
            ImGui::TextColored(P.muted, "O mouse usa Raw Input em modo relativo: sem aceleração do Windows e sem limite de borda.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Atalhos")) {
            auto& h = cfg_.hotkeys;
            struct HK { const char* label; std::string* v; } hks[] = {
                {"Mostrar controles", &h.showControls}, {"Esconder controles", &h.hideControls},
                {"Ativar/desativar modo mouse (mapeamento)", &h.toggleMapping}, {"Liberar mouse", &h.releaseMouse},
                {"Capturar/liberar mouse", &h.captureMouse}, {"Abrir configurações", &h.openSettings},
                {"Painel de performance", &h.togglePerf}, {"Tela cheia", &h.fullscreen}};
            if (ImGui::BeginTable("##hk", 2, ImGuiTableFlags_RowBg)) {
                for (auto& k : hks) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(k.label);
                    ImGui::TableNextColumn(); keyCaptureButton(k.label, *k.v, false);
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Restaurar padrões")) { cfg_.hotkeys = HotkeySettings{}; saveSettings(); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Interface")) {
            int theme = cfg_.ui.darkTheme ? 0 : 1;
            if (ImGui::RadioButton("Escuro", &theme, 0)) { cfg_.ui.darkTheme = true; themeDirty_ = true; saveSettings(); }
            ImGui::SameLine();
            if (ImGui::RadioButton("Claro", &theme, 1)) { cfg_.ui.darkTheme = false; themeDirty_ = true; saveSettings(); }
            static float pendingScale = -1;
            if (pendingScale < 0) pendingScale = cfg_.ui.uiScale;
            ImGui::SetNextItemWidth(220 * s);
            ImGui::SliderFloat("Escala da interface", &pendingScale, 0.75f, 2.0f, "%.2fx");
            if (ImGui::IsItemDeactivatedAfterEdit()) { cfg_.ui.uiScale = pendingScale; saveSettings(); }
            ImGui::Text("DPI do monitor: %.0f%%", dpiScale_ * 100);
            if (ImGui::Checkbox("Splash screen ao iniciar", &cfg_.ui.showSplash)) saveSettings();
            if (ImGui::Checkbox("Painel de performance sobre o jogo", &showPerf_)) saveSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Conexão")) {
            if (ImGui::Checkbox("Reconexão automática", &cfg_.autoReconnect)) saveSettings();
            if (ImGui::Checkbox("Manter tela do celular ligada durante o uso", &cfg_.stayAwake)) saveSettings();
            if (ImGui::Checkbox("Mostrar toques (necessário para o teste de latência)", &cfg_.showTouchesForTest)) saveSettings();
            static char adbBuf[260];
            if (!adbBuf[0] && !cfg_.adbPath.empty()) std::snprintf(adbBuf, sizeof adbBuf, "%s", cfg_.adbPath.c_str());
            ImGui::SetNextItemWidth(420 * s);
            if (ImGui::InputTextWithHint("Caminho do adb", "automático (platform-tools ao lado do .exe ou PATH)", adbBuf, sizeof adbBuf))
                { cfg_.adbPath = adbBuf; saveSettings(); }
            ImGui::TextColored(P.muted, "Somente USB é usado: o túnel é um adb forward sobre o cabo.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sobre")) {
            drawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 72 * s);
            ImGui::Dummy(ImVec2(72 * s, 72 * s));
            ImGui::PushFont(fonts().title); ImGui::Text("%s %s", MOB_APP_NAME, MOB_VERSION); ImGui::PopFont();
            ImGui::TextColored(P.accent, "%s", MOB_APP_SUBTITLE);
            ImGui::TextWrapped("Espelhamento e controle legítimos: transmite a tela e envia os comandos do usuário. "
                               "Sem modificação de APK, injeção de código, leitura de memória ou automação.");
            ImGui::TextWrapped("Componente no Android: scrcpy-server %s (Apache 2.0, Genymobile).", "3.1");
            ImGui::TextColored(P.muted, "Configurações: %s", cfgPath_.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------- diagnóstico
void App::viewDiagnostics() {
    const Palette& P = pal();
    sectionTitle("Diagnóstico", "Verificação do ambiente e logs");
    std::vector<AdbDevice> devs;
    { std::lock_guard<std::mutex> lk(devMx_); devs = devices_; }
    auto check = [&](bool ok, const char* name, const std::string& detail, bool warn = false) {
        statusDot(ok, warn);
        ImGui::Text("%s", name);
        ImGui::SameLine(300 * fontScaleBuilt_);
        ImGui::TextColored(ok ? P.muted : (warn ? P.warn : P.danger), "%s", detail.c_str());
    };
    if (beginCard("##chk")) {
        check(adbReady_, "ADB", adbReady_ ? adb_.path() + "  —  " + adbVersion_ : "não encontrado (instale platform-tools)");
        std::string sp = Session::serverPath();
        check(!sp.empty(), "scrcpy-server", sp.empty() ? "arquivo ausente ao lado do executável" : sp);
        bool any = false, unauth = false;
        for (auto& d : devs) { if (d.state == "device") any = true; if (d.state == "unauthorized") unauth = true; }
        check(any, "Dispositivo autorizado", any ? std::to_string(devs.size()) + " dispositivo(s)" :
              unauth ? "aceite a chave RSA na tela do celular" : "conecte o cabo e ative a Depuração USB", unauth);
        check(true, "Renderer", rendererName_ + (vsyncApplied_ ? " (VSync ligado)" : " (VSync desligado)"));
        std::string dn = session_->decoderName();
        check(session_->state() == SessionState::Streaming, "Decodificador", dn.empty() ? "inativo" : dn, !snap_.hwDecode);
        check(true, "Timer do sistema", "resolução de 1 ms solicitada (timeBeginPeriod)");
        for (auto& d : devs)
            ImGui::BulletText("%s  [%s]  %s  %s", d.serial.c_str(), d.state.c_str(), d.model.c_str(), d.usbPath.c_str());
    }
    endCard();
    ImGui::BeginDisabled(!adbReady_);
    if (ImGui::Button("Reiniciar servidor ADB")) {
        disconnect();
        adb_.run("", {"kill-server"}, 5000);
        adb_.startServer();
        LOGI("servidor ADB reiniciado");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Abrir pasta de logs")) { std::string url = "file:///" + appDataDir(); SDL_OpenURL(url.c_str()); }
    ImGui::SameLine();
    auto lines = Log::recent();
    if (ImGui::Button("Copiar logs")) {
        std::string all;
        for (auto& l : lines) all += l + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    if (ImGui::BeginChild("##log", ImVec2(0, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders)) {
        ImGui::PushFont(fonts().mono);
        for (auto& l : lines) {
            ImVec4 c = l.find("ERROR") != std::string::npos ? P.danger : l.find("WARN") != std::string::npos ? P.warn : P.text;
            ImGui::TextColored(c, "%s", l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
    }
    ImGui::EndChild();
}
}

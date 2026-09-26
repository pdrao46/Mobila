# MOBILADOR — Arquitetura de baixa latência

> Premissa: não existe "zero delay". O objetivo é **eliminar etapas, filas e cópias desnecessárias** e **medir** o que sobra.

## 1. Visão geral

```
ANDROID (shell, via app_process)                          WINDOWS (Mobilador.exe, C++20)
┌──────────────────────────────┐                ┌──────────────────────────────────────────────┐
│ SurfaceFlinger ──(VirtualDisplay, GPU)──┐     │ mob-video thread (MMCSS "Games")             │
│                                         ▼     │   recv(header 12B) → recv(corpo) ─┐          │
│             MediaCodec HW (H.264/H.265/AV1)   │   FFmpeg decode (D3D11VA ou CPU)   │ sem fila │
│             KEY_PRIORITY=0, KEY_LATENCY=0     │   → FrameQueue (mailbox, 1 slot) ◄─┘          │
│                    │                          │                 │ SDL user event (1 pendente)│
│  socket abstrato ──┘                          │ thread principal (eventos + render)          │
│  scrcpy-server 3.1 (Apache 2.0)               │   input → mensagem 32 B → send() imediato     │
│  injectInputEvent ◄───── socket de controle ──┼── TCP_NODELAY                                │
└──────────────────────────────┘   USB (adb forward)   upload YUV → shader YUV→RGB → Present(flip) │
                                                └──────────────────────────────────────────────┘
```

## 2. Tecnologias e justificativa

| Etapa | Escolha | Por quê |
|---|---|---|
| Captura/encode no Android | scrcpy-server 3.1 | Roda como `shell` (sem root, sem APK, sem modificar jogo), usa VirtualDisplay + MediaCodec de hardware — caminho de GPU a encoder sem passar pela CPU. Protocolo binário enxuto. Reimplementar isso não reduziria latência. |
| Transporte | `adb forward` sobre USB | Único transporte USB disponível sem root/driver próprio. O túnel é aberto uma vez; depois é só um fluxo TCP loopback→USB. ADB **não** é usado por evento. |
| Decodificação | FFmpeg `libavcodec` + D3D11VA | Decoder de GPU com fallback automático para CPU. `AV_CODEC_FLAG_LOW_DELAY`, threading de *slice* (threading de *frame* soma 1 frame de atraso por thread). |
| Renderização | SDL2 renderer Direct3D 11 | Swapchain flip-model DXGI, textura YUV (NV12/IYUV) com conversão para RGB em pixel shader — nenhuma conversão de cor na CPU. VSync opcional. |
| UI | Dear ImGui (1.91.9) | Modo imediato, desenhada no mesmo renderer e no mesmo present do vídeo: sem compositor extra, sem segunda janela, sem framework pesado (WPF/Qt/Electron adicionariam composição e cópias). |
| Input | Protocolo de controle do scrcpy (`InputManager.injectInputEvent`) | Veja §5. |
| Linguagem | C++20 | Controle total de threads, memória e cópias. |

## 3. Fluxo do vídeo (e onde cada fonte de delay foi tratada)

1. **Captura** — VirtualDisplay espelha a tela direto na Surface de entrada do encoder (GPU→encoder, zero cópia no Android).
2. **Codificação** — encoder de hardware; em ULTRA LOW LATENCY enviamos `priority=0` (tempo real) e `latency=0` (sem fila interna, Android 11+). Resolução menor (`max_size`) reduz o custo por frame em *todas* as etapas seguintes.
3. **USB** — cada pacote chega com cabeçalho `pts|flags|tamanho`. Medimos o tempo cabeçalho→último byte (latência de transferência real).
4. **Buffers** — **não existe fila de pacotes**: a mesma thread que recebe decodifica imediatamente. SO_RCVBUF de 1 MB apenas evita perda com picos de keyframe.
5. **Decodificação** — pacote aponta direto para o buffer de recepção (sem cópia extra; o buffer é reutilizado, só cresce até o maior pacote). SPS/PPS são prefixados apenas no pacote seguinte (raro).
6. **Memória** — AVFrames pré-alocados na FrameQueue; `av_frame_move_ref` troca ponteiros (sem memcpy). Com D3D11VA há exatamente **uma** cópia GPU→RAM (NV12), necessária porque o renderer do SDL não aceita textura D3D11 externa; ver "Próximos passos".
7. **Frame mais recente** — FrameQueue com capacidade 1 (ULL/BALANCED): se o render não consumiu, o frame antigo é descartado e contado em "frames descartados". QUALITY permite 2 para suavidade.
8. **Renderização** — o upload da textura acontece **somente para o frame que será exibido** (frames descartados nunca vão para a GPU).
9. **VSync / frame pacing** — ULL: VSync desligado (apresenta imediatamente; pode haver tearing). QUALITY: VSync ligado (sem tearing, até 1 refresh a mais). A thread principal é orientada a eventos (`SDL_WaitEventTimeout`): acorda quando chega frame ou input — não há timer fixo introduzindo espera.
10. **Compositor do Windows** — flip-model permite *independent flip* em tela cheia (F11), removendo a cópia do DWM.
11. **Energia/CPU** — `timeBeginPeriod(1)` e MMCSS "Games" na thread de vídeo evitam o agendador de 15,6 ms e o *parking* de núcleos. Nenhuma thread faz *busy-wait*.

## 4. Gerenciamento de threads

| Thread | Função | Bloqueia em |
|---|---|---|
| principal | eventos SDL, input, UI, upload/present | `SDL_WaitEventTimeout` (acorda por evento) |
| mob-video | recv + decode | `recv()` |
| mob-ctrlrx | drena mensagens do aparelho (clipboard/acks) | `recv()` |
| watcher | `adb devices -l` a cada 1,5 s | sleep |
| info | consultas pontuais (getprop, encoders, temperatura) | processo adb |

Sincronização entre vídeo e UI: um mutex segurado só durante troca de ponteiros + um único evento SDL pendente (flag atômica), então eventos nunca se acumulam.

## 5. Fluxo do input — análise das alternativas

| Método | Latência típica | Veredito |
|---|---|---|
| `adb shell input tap` | 100–300 ms (inicia uma JVM por comando) | descartado |
| `adb shell sendevent` | dezenas de ms, exige root/permissões de `/dev/input` | descartado |
| AOA/HID via USB (scrcpy OTG) | ~1 ms, HID real | teclado/mouse HID não produzem **toques**; jogos mobile esperam toque multi-touch. Mouse HID mostra cursor. Útil só para teclado de texto. |
| UHID (scrcpy `uhid`) | baixo | mesmo problema: é HID de teclado/mouse/gamepad, não touch |
| **`InputManager.injectInputEvent` via socket persistente** | **sub-ms no PC + injeção nativa** | **escolhido**: gera `MotionEvent` multi-touch idênticos a dedos reais, com IDs de ponteiro independentes |

Implementação:
- Evento do SO → `InputMapper` → mensagem binária de 32 bytes → `send()` **na mesma thread, na hora** (TCP_NODELAY, sem fila, sem timer).
- Teclas: *auto-repeat* do SO é ignorado; cada tecla gera no máximo 1 mensagem.
- Mouse: Raw Input em modo relativo (sem aceleração do Windows, sem limite de borda). Todos os deltas de um lote de eventos viram **um** MOVE (o Android funde movimentos no mesmo vsync de qualquer jeito; mandar 1000 msgs/s só criaria fila).
- Câmera "infinita": dedo virtual dentro da área de câmera; ao atingir a borda, levanta e recoloca no centro.
- Joystick WASD: um dedo no centro, move para a direção normalizada (diagonais corretas), modificador de caminhada reduz o raio.
- Perda de foco da janela → todos os dedos são levantados (sem toques presos).

## 6. Gerenciamento de memória

- Buffers de recepção, AVPacket e AVFrames alocados uma vez e reutilizados.
- Logs com ring buffer fixo (500 linhas) e arquivo rotativo (4 MB).
- Histórico de frame time: array fixo de 300 amostras; janela de estatística limitada a 2000.
- Sem alocação por frame no caminho quente → RAM estável em sessões longas.

## 7. Comunicação USB e recuperação

1. `adb push` do servidor (≈90 KB) → `adb forward tcp:<porta aleatória> localabstract:scrcpy_<scid>` → `adb shell app_process ...`
2. Conexão do socket de vídeo; o *dummy byte* confirma que o servidor está escutando (sem sleeps fixos).
3. Falha (cabo, ADB reiniciado, aparelho bloqueado) → estado *Error* → reconexão automática com backoff (0,7 s → 5 s) assim que o aparelho reaparece como `device`.

## 8. Diagnóstico — o que é medido e como

| Métrica | Fonte |
|---|---|
| USB | cabeçalho→último byte de cada pacote |
| Jitter do pipeline | `(chegada − pts)` menos a melhor linha base dos últimos 10 s. Relógios diferentes impedem valor absoluto; a variação revela fila no encoder/USB |
| Decode | send_packet→frame pronto (inclui cópia GPU→RAM) |
| Render | fila + upload + present |
| Input | timestamp do evento no SO → mensagem enviada (resolução 1 ms do SDL2) |
| **Ponta a ponta** | toque injetado → Android desenha indicador ("mostrar toques") → captura → encode → USB → decode → detectado por mudança de luminância na região. **Medição real, não estimativa.** |
| Captura+encode | ponta a ponta − (USB + decode + input). Inclui injeção e render do próprio Android |
| CPU/RAM | `GetProcessTimes`, `GetProcessMemoryInfo` |
| GPU | contadores PDH `GPU Engine(pid_*)` |
| Temperatura | `dumpsys battery` a cada 20 s |

Não medido: scan-out do monitor e o tempo que o jogo leva para reagir (inclusos no "captura+encode" se o alvo for o jogo).

## 9. Benchmark

Executa cada configuração (A/B/C, editáveis) com a mesma metodologia: reinicia o stream → aquecimento → medição de N segundos (snapshots de 1 s) → amostras ponta a ponta. Mostra FPS médio/mínimo/desvio, frame time médio/desvio/p99, frames perdidos, bitrate, cada etapa, CPU/GPU/RAM e latência ponta a ponta. O melhor valor de cada linha é destacado, **nada é aplicado automaticamente**. Exporta CSV.

## 10. Próximos passos de otimização (medir antes)

- **Zero-copy D3D11**: renderer D3D11 próprio compartilhando o `ID3D11Device` do FFmpeg e amostrando a textura NV12 do decoder diretamente (elimina a cópia GPU→RAM + upload, ~1–3 ms em 1080p).
- Waitable swapchain (`DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`) para VSync com 1 frame de latência máxima.
- SDL3 (timestamps de evento em ns).

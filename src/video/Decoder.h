#pragma once
#include <cstdint>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
}

namespace mob {
// Decodificador FFmpeg configurado para latência mínima:
//  - AV_CODEC_FLAG_LOW_DELAY, sem threads de frame (frame threading adiciona 1 frame de atraso por thread);
//  - D3D11VA (decodificação na GPU) quando disponível, fallback automático para software;
//  - o frame sai na mesma chamada do pacote (o encoder do Android não gera B-frames aqui).
class Decoder {
public:
    ~Decoder() { close(); }
    bool open(uint32_t scrcpyCodecId, bool preferHw, bool allowHw);
    void close();
    // Envia um pacote e, se um frame ficar pronto, retorna-o (em memória de sistema, NV12 ou YUV420P).
    AVFrame* decode(const uint8_t* data, int size, int64_t pts, bool key);
    bool isHw() const { return hw_ != nullptr; }
    std::string name() const;
private:
    AVCodecContext* ctx_ = nullptr;
    AVBufferRef* hw_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* sw_ = nullptr;
};
}

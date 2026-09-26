#include "Decoder.h"
#include "../core/Log.h"
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace mob {
static enum AVPixelFormat g_hwFmt = AV_PIX_FMT_NONE;
static enum AVPixelFormat pickFormat(AVCodecContext*, const enum AVPixelFormat* fmts) {
    for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == g_hwFmt) return *p;
    LOGW("formato de hardware indisponível, usando software");
    return fmts[0];
}

bool Decoder::open(uint32_t id, bool preferHw, bool allowHw) {
    close();
    AVCodecID cid = AV_CODEC_ID_H264;
    if (id == 0x68323635) cid = AV_CODEC_ID_HEVC;       // "h265"
    else if (id == 0x00617631) cid = AV_CODEC_ID_AV1;   // "av1"
    const AVCodec* codec = avcodec_find_decoder(cid);
    if (!codec) { LOGE("decoder não encontrado para codec 0x%08x", id); return false; }
    ctx_ = avcodec_alloc_context3(codec);
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx_->thread_type = FF_THREAD_SLICE;  // nunca FF_THREAD_FRAME (adiciona latência)
    ctx_->thread_count = 0;

    if (allowHw && preferHw) {
#ifdef _WIN32
        const AVHWDeviceType types[] = {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
#else
        const AVHWDeviceType types[] = {AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_NONE};
#endif
        for (auto t : types) {
            if (t == AV_HWDEVICE_TYPE_NONE) break;
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
                if (!cfg) break;
                if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == t) {
                    if (av_hwdevice_ctx_create(&hw_, t, nullptr, nullptr, 0) == 0) {
                        g_hwFmt = cfg->pix_fmt;
                        ctx_->hw_device_ctx = av_buffer_ref(hw_);
                        ctx_->get_format = pickFormat;
                        ctx_->extra_hw_frames = 0;  // pool mínimo
                    }
                    break;
                }
            }
            if (hw_) break;
        }
        if (!hw_) LOGW("decodificação por hardware indisponível — usando CPU");
    }
    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        LOGE("falha ao abrir decoder");
        close();
        return false;
    }
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    sw_ = av_frame_alloc();
    LOGI("decoder %s aberto (%s)", codec->name, hw_ ? "GPU" : "CPU");
    return true;
}

void Decoder::close() {
    avcodec_free_context(&ctx_);
    av_buffer_unref(&hw_);
    av_packet_free(&pkt_);
    av_frame_free(&frame_);
    av_frame_free(&sw_);
}

std::string Decoder::name() const {
    if (!ctx_) return "-";
    return std::string(ctx_->codec->name) + (hw_ ? " + D3D11VA" : " (CPU)");
}

AVFrame* Decoder::decode(const uint8_t* data, int size, int64_t pts, bool key) {
    if (!ctx_) return nullptr;
    // Sem cópia: o pacote aponta diretamente para o buffer de recepção (sem refcount;
    // avcodec copia internamente apenas se precisar reter dados).
    pkt_->data = const_cast<uint8_t*>(data);
    pkt_->size = size;
    pkt_->pts = pts;
    pkt_->dts = pts;
    pkt_->flags = key ? AV_PKT_FLAG_KEY : 0;
    int r = avcodec_send_packet(ctx_, pkt_);
    pkt_->data = nullptr; pkt_->size = 0;
    if (r < 0 && r != AVERROR(EAGAIN)) return nullptr;
    AVFrame* last = nullptr;
    // Drena tudo; se sair mais de um frame, somente o último interessa.
    while (avcodec_receive_frame(ctx_, frame_) == 0) {
        if (frame_->format == g_hwFmt && hw_) {
            // Única transferência GPU->RAM (NV12). O shader do renderer faz YUV->RGB na GPU.
            av_frame_unref(sw_);
            if (av_hwframe_transfer_data(sw_, frame_, 0) < 0) { av_frame_unref(frame_); continue; }
            sw_->pts = frame_->pts;
            av_frame_unref(frame_);
            last = sw_;
        } else {
            last = frame_;
        }
        if (last == frame_) break;  // frame_ seria sobrescrito na próxima iteração
    }
    return last;
}
}

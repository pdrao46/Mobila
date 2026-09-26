#pragma once
#include <mutex>
#include <vector>
#include <atomic>
extern "C" {
#include <libavutil/frame.h>
}

namespace mob {
// Fila de frames decodificados com capacidade pequena e fixa (1..3).
// Capacidade 1 = "mailbox": o decoder sempre substitui o frame pendente, então a
// renderização sempre pega o frame MAIS RECENTE e nunca acumula atraso.
// Os AVFrame são pré-alocados e reutilizados (sem malloc por frame; av_frame_move_ref só troca ponteiros).
class FrameQueue {
public:
    explicit FrameQueue(int capacity = 1) { setCapacity(capacity); }
    ~FrameQueue() {
        for (auto* f : ring_) av_frame_free(&f);
        av_frame_free(&out_);
    }
    void setCapacity(int c) {
        std::lock_guard<std::mutex> lk(mx_);
        c = c < 1 ? 1 : (c > 3 ? 3 : c);
        for (auto* f : ring_) av_frame_free(&f);
        ring_.assign(c, nullptr);
        for (auto*& f : ring_) f = av_frame_alloc();
        if (!out_) out_ = av_frame_alloc();
        head_ = count_ = 0;
    }
    // Decoder: move a referência de src para a fila. Retorna true se descartou um frame antigo.
    bool push(AVFrame* src) {
        std::lock_guard<std::mutex> lk(mx_);
        bool dropped = false;
        int cap = (int)ring_.size();
        if (count_ == cap) {  // descarta o mais antigo
            av_frame_unref(ring_[head_]);
            head_ = (head_ + 1) % cap;
            --count_;
            dropped = true;
        }
        int tail = (head_ + count_) % cap;
        av_frame_unref(ring_[tail]);
        av_frame_move_ref(ring_[tail], src);
        ++count_;
        return dropped;
    }
    // Render: pega o próximo frame (ou nullptr). O ponteiro é válido até o próximo pop.
    AVFrame* pop() {
        std::lock_guard<std::mutex> lk(mx_);
        if (!count_) return nullptr;
        av_frame_unref(out_);
        av_frame_move_ref(out_, ring_[head_]);
        head_ = (head_ + 1) % (int)ring_.size();
        --count_;
        return out_;
    }
    int size() { std::lock_guard<std::mutex> lk(mx_); return count_; }
    void clear() {
        std::lock_guard<std::mutex> lk(mx_);
        for (auto* f : ring_) av_frame_unref(f);
        head_ = count_ = 0;
    }
private:
    std::mutex mx_;
    std::vector<AVFrame*> ring_;
    AVFrame* out_ = nullptr;
    int head_ = 0, count_ = 0;
};
}

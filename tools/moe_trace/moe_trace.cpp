// moe_trace: record the experts llama.cpp's router selects, per token and
// layer, as ground truth for moepager's simulator (Phase 7, experiment-only).
//
// It reads the `ffn_moe_topk-<layer>` tensors through the scheduler's
// eval callback. That tensor is an argsort + *view*: each row is n_expert
// wide in memory but only the first n_expert_used entries are the
// selection, so rows are read one by one using nb[1].
//
// Output CSV: token,layer,expert,t_ns  (decode tokens only unless --prompt-too)
//
// usage: moe_trace -m model.gguf -o trace.csv [-n 256] [-t threads]
//                  [--no-repack] [--prompt-too] [-p "prompt"]

#include "ggml-backend.h"
#include "ggml.h"
#include "llama.h"

#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Capture {
    FILE * out = nullptr;
    int64_t t0 = 0;
    int64_t token_base = 0;  // token index of the first row of the current batch
    bool prompt_phase = true;
    bool prompt_too = false;
    int64_t rows = 0;
    std::vector<int32_t> row;
};

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static bool on_eval(struct ggml_tensor * t, bool ask, void * ud) {
    auto * c = static_cast<Capture *>(ud);
    const bool want = std::strncmp(t->name, "ffn_moe_topk", 12) == 0;
    if (ask || !want) {
        return want || !ask;  // ask: only these tensors; otherwise keep running
    }
    if (c->prompt_phase && !c->prompt_too) {
        return true;
    }
    if (t->type != GGML_TYPE_I32) {
        std::fprintf(stderr, "moe_trace: %s has type %d, expected I32\n", t->name, t->type);
        return true;
    }
    const char * dash = std::strrchr(t->name, '-');
    const int layer = dash ? std::atoi(dash + 1) : -1;
    const int64_t k = t->ne[0];
    const int64_t n = t->ne[1];
    const int64_t ts = now_ns() - c->t0;
    c->row.resize(k);
    for (int64_t j = 0; j < n; ++j) {
        ggml_backend_tensor_get(t, c->row.data(), j * t->nb[1], k * sizeof(int32_t));
        for (int64_t i = 0; i < k; ++i) {
            std::fprintf(c->out, "%lld,%d,%d,%lld\n", (long long) (c->token_base + j), layer,
                         c->row[i], (long long) ts);
            c->rows++;
        }
    }
    return true;
}

static void batch_set(llama_batch_ext * b, const llama_token * tok, int32_t n, llama_pos pos0) {
    llama_batch_ext_clear(b);
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = llama_batch_ext_add_token(b, 0, tok[i]);
        const llama_pos pos = pos0 + i;
        llama_batch_ext_set_pos(b, idx, &pos);
    }
    llama_batch_ext_set_output_logits(b, n - 1, true);
}

int main(int argc, char ** argv) {
    std::setlocale(LC_NUMERIC, "C");
    std::string model, outp = "trace.csv";
    std::string prompt = "Explain, step by step, how an operating system manages virtual memory.";
    int n_predict = 256, n_threads = 0;
    bool no_repack = false, prompt_too = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "-m") model = next();
        else if (a == "-o") outp = next();
        else if (a == "-p") prompt = next();
        else if (a == "-n") n_predict = std::atoi(next().c_str());
        else if (a == "-t") n_threads = std::atoi(next().c_str());
        else if (a == "--no-repack") no_repack = true;
        else if (a == "--prompt-too") prompt_too = true;
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (model.empty()) {
        std::fprintf(stderr, "usage: %s -m model.gguf -o trace.csv [-n N] [-t T] [-p prompt] [--no-repack] [--prompt-too]\n", argv[0]);
        return 2;
    }

    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    mp.use_extra_bufts = !no_repack;
    llama_model * m = llama_model_load_from_file(model.c_str(), mp);
    if (!m) { std::fprintf(stderr, "failed to load %s\n", model.c_str()); return 1; }
    const llama_vocab * vocab = llama_model_get_vocab(m);

    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), nullptr, 0, true, true);
    std::vector<llama_token> toks(n_prompt);
    llama_tokenize(vocab, prompt.c_str(), prompt.size(), toks.data(), toks.size(), true, true);

    Capture cap;
    cap.out = std::fopen(outp.c_str(), "w");
    if (!cap.out) { std::perror(outp.c_str()); return 1; }
    std::fprintf(cap.out, "token,layer,expert,t_ns\n");
    cap.prompt_too = prompt_too;

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_prompt + n_predict + 8;
    cp.n_batch = n_prompt;
    cp.no_perf = false;
    cp.cb_eval = on_eval;
    cp.cb_eval_user_data = &cap;
    if (n_threads > 0) { cp.n_threads = n_threads; cp.n_threads_batch = n_threads; }
    llama_context * ctx = llama_init_from_model(m, cp);
    if (!ctx) { std::fprintf(stderr, "failed to create context\n"); return 1; }

    auto sp = llama_sampler_chain_default_params();
    llama_sampler * smpl = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    llama_batch_ext * batch = llama_batch_ext_init(ctx);
    batch_set(batch, toks.data(), n_prompt, 0);
    cap.t0 = now_ns();
    int n_tokens = n_prompt, n_pos = 0, n_gen = 0;
    while (n_gen < n_predict) {
        cap.prompt_phase = (n_pos == 0);
        cap.token_base = prompt_too ? n_pos : (cap.prompt_phase ? 0 : n_gen - 1);
        if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch)) {
            std::fprintf(stderr, "decode failed\n");
            return 1;
        }
        n_pos += n_tokens;
        llama_token id = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) break;
        char buf[256];
        int nb = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (nb > 0) std::fwrite(buf, 1, nb, stderr);
        batch_set(batch, &id, 1, n_pos);
        n_tokens = 1;
        n_gen++;
    }
    std::fprintf(stderr, "\nmoe_trace: %d tokens generated, %lld expert rows -> %s\n", n_gen,
                 (long long) cap.rows, outp.c_str());
    llama_perf_context_print(ctx);
    std::fclose(cap.out);
    llama_batch_ext_free(batch);
    llama_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(m);
    llama_backend_free();
    return 0;
}

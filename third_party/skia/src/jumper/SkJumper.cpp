/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkColorPriv.h"
#include "SkCpu.h"
#include "SkJumper.h"
#include "SkOnce.h"
#include "SkRasterPipeline.h"
#include "SkTemplates.h"

// We'll use __has_feature(memory_sanitizer) to detect MSAN.
// SkJumper_generated.S is not compiled with MSAN, so MSAN would yell really loud.
#if !defined(__has_feature)
    #define __has_feature(x) 0
#endif

#define M(st) +1
static const int kNumStages = SK_RASTER_PIPELINE_STAGES(M);
#undef M

#ifndef SK_JUMPER_DISABLE_8BIT
    #if 0 && !__has_feature(memory_sanitizer) && (defined(__x86_64__) || defined(_M_X64))
        #include <atomic>

        #define M(st) #st,
        static const char* kStageNames[] = { SK_RASTER_PIPELINE_STAGES(M) };
        #undef M

        static std::atomic<int> gMissingStageCounters[kNumStages];

        static void log_missing(SkRasterPipeline::StockStage st) {
            static SkOnce once;
            once([] { atexit([] {
                for (int i = 0; i < kNumStages; i++) {
                    if (int count = gMissingStageCounters[i].load()) {
                        SkDebugf("%7d\t%s\n", count, kStageNames[i]);
                    }
                }
            }); });

            gMissingStageCounters[st]++;
        }
    #else
        static void log_missing(SkRasterPipeline::StockStage) {}
    #endif
#endif

// We can't express the real types of most stage functions portably, so we use a stand-in.
// We'll only ever call start_pipeline(), which then chains into the rest.
using StageFn         = void(void);
using StartPipelineFn = void(size_t,size_t,size_t,size_t, void**);

// Some platforms expect C "name" maps to asm "_name", others to "name".
#if defined(__APPLE__)
    #define ASM(name, suffix)  sk_##name##_##suffix
#else
    #define ASM(name, suffix) _sk_##name##_##suffix
#endif

// Some stages have 8-bit versions from SkJumper_stages_8bit.cpp.
#define LOWP_STAGES(M)   \
    M(black_color) M(white_color) M(uniform_color) \
    M(set_rgb)           \
    M(premul)            \
    M(load_8888) M(load_8888_dst) M(store_8888) \
    M(load_bgra) M(load_bgra_dst) M(store_bgra) \
    M(load_a8)   M(load_a8_dst)   M(store_a8)   \
    M(load_g8)   M(load_g8_dst)                 \
    M(swap_rb)           \
    M(srcover_rgba_8888) \
    M(lerp_1_float)      \
    M(lerp_u8)           \
    M(scale_1_float)     \
    M(scale_u8)          \
    M(move_src_dst)      \
    M(move_dst_src)      \
    M(clear)             \
    M(srcatop)           \
    M(dstatop)           \
    M(srcin)             \
    M(dstin)             \
    M(srcout)            \
    M(dstout)            \
    M(srcover)           \
    M(dstover)           \
    M(modulate)          \
    M(multiply)          \
    M(screen)            \
    M(xor_)              \
    M(plus_)             \
    M(darken)            \
    M(lighten)           \
    M(difference)        \
    M(exclusion)         \
    M(hardlight)         \
    M(overlay)

extern "C" {

#if __has_feature(memory_sanitizer)
    // We'll just run baseline code.

#elif defined(__arm__)
    StartPipelineFn ASM(start_pipeline,vfp4);
    StageFn ASM(just_return,vfp4);
    #define M(st) StageFn ASM(st,vfp4);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M

#elif defined(__x86_64__) || defined(_M_X64)
    StartPipelineFn ASM(start_pipeline,hsw       ),
                    ASM(start_pipeline,avx       ),
                    ASM(start_pipeline,sse41     ),
                    ASM(start_pipeline,sse2      ),
                    ASM(start_pipeline,hsw_8bit  ),
                    ASM(start_pipeline,sse41_8bit),
                    ASM(start_pipeline,sse2_8bit );

    StageFn ASM(just_return,hsw),
            ASM(just_return,avx),
            ASM(just_return,sse41),
            ASM(just_return,sse2),
            ASM(just_return,hsw_8bit  ),
            ASM(just_return,sse41_8bit),
            ASM(just_return,sse2_8bit );

    #define M(st) StageFn ASM(st,hsw);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,avx);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,sse41);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,sse2);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M

    #define M(st) StageFn ASM(st,hsw_8bit);
        LOWP_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,sse41_8bit);
        LOWP_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,sse2_8bit);
        LOWP_STAGES(M)
    #undef M

#elif defined(__i386__) || defined(_M_IX86)
    StartPipelineFn ASM(start_pipeline,sse2),
                    ASM(start_pipeline,sse2_8bit);
    StageFn ASM(just_return,sse2),
            ASM(just_return,sse2_8bit);
    #define M(st) StageFn ASM(st,sse2);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
    #define M(st) StageFn ASM(st,sse2_8bit);
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M

#endif

    // Baseline code compiled as a normal part of Skia.
    StartPipelineFn sk_start_pipeline;
    StageFn sk_just_return;
    #define M(st) StageFn sk_##st;
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M

#if defined(JUMPER_HAS_NEON_8BIT)
    // We also compile 8-bit stages on ARMv8 as a normal part of Skia when compiled with Clang.
    StartPipelineFn sk_start_pipeline_8bit;
    StageFn sk_just_return_8bit;
    #define M(st) StageFn sk_##st##_8bit;
        SK_RASTER_PIPELINE_STAGES(M)
    #undef M
#endif

}

#if !__has_feature(memory_sanitizer) && (defined(__x86_64__) || defined(_M_X64))
    template <SkRasterPipeline::StockStage st>
    static constexpr StageFn* hsw_8bit() { return nullptr; }

    template <SkRasterPipeline::StockStage st>
    static constexpr StageFn* sse41_8bit() { return nullptr; }

    template <SkRasterPipeline::StockStage st>
    static constexpr StageFn* sse2_8bit() { return nullptr; }

    #define M(st) \
        template <> constexpr StageFn* hsw_8bit<SkRasterPipeline::st>() {   \
            return ASM(st,hsw_8bit);                                        \
        }                                                                   \
        template <> constexpr StageFn* sse41_8bit<SkRasterPipeline::st>() { \
            return ASM(st,sse41_8bit);                                      \
        }                                                                   \
        template <> constexpr StageFn* sse2_8bit<SkRasterPipeline::st>() {  \
            return ASM(st,sse2_8bit);                                       \
        }
        LOWP_STAGES(M)
    #undef M
#elif !defined(SK_JUMPER_LEGACY_X86_8BIT) && \
    (defined(__i386__) || defined(_M_IX86))
    template <SkRasterPipeline::StockStage st>
    static constexpr StageFn* sse2_8bit() { return nullptr; }

    #define M(st) \
        template <> constexpr StageFn* sse2_8bit<SkRasterPipeline::st>() {  \
            return ASM(st,sse2_8bit);                                       \
        }
        LOWP_STAGES(M)
    #undef M

#elif defined(JUMPER_HAS_NEON_8BIT)
    template <SkRasterPipeline::StockStage st>
    static constexpr StageFn* neon_8bit() { return nullptr; }

    #define M(st)                                                            \
        template <> constexpr StageFn* neon_8bit<SkRasterPipeline::st>() {   \
            return sk_##st##_8bit;                                           \
        }
        LOWP_STAGES(M)
    #undef M
#endif

// Engines comprise everything we need to run SkRasterPipelines.
struct SkJumper_Engine {
    StageFn*         stages[kNumStages];
    StartPipelineFn* start_pipeline;
    StageFn*         just_return;
};

// We'll default to this baseline engine, but try to choose a better one at runtime.
static const SkJumper_Engine kBaseline = {
#define M(stage) sk_##stage,
    { SK_RASTER_PIPELINE_STAGES(M) },
#undef M
    sk_start_pipeline,
    sk_just_return,
};
static SkJumper_Engine gEngine = kBaseline;
static SkOnce gChooseEngineOnce;

static SkJumper_Engine choose_engine() {
#if __has_feature(memory_sanitizer)
    // We'll just run baseline code.

#elif defined(__arm__)
    if (1 && SkCpu::Supports(SkCpu::NEON|SkCpu::NEON_FMA|SkCpu::VFP_FP16)) {
        return {
        #define M(stage) ASM(stage, vfp4),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }

#elif defined(__x86_64__) || defined(_M_X64)
    if (1 && SkCpu::Supports(SkCpu::HSW)) {
        return {
        #define M(stage) ASM(stage, hsw),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }
    if (1 && SkCpu::Supports(SkCpu::AVX)) {
        return {
        #define M(stage) ASM(stage, avx),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }
    if (1 && SkCpu::Supports(SkCpu::SSE41)) {
        return {
        #define M(stage) ASM(stage, sse41),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }
    if (1 && SkCpu::Supports(SkCpu::SSE2)) {
        return {
        #define M(stage) ASM(stage, sse2),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }

#elif defined(__i386__) || defined(_M_IX86)
    if (1 && SkCpu::Supports(SkCpu::SSE2)) {
        return {
        #define M(stage) ASM(stage, sse2),
            { SK_RASTER_PIPELINE_STAGES(M) },
            M(start_pipeline)
            M(just_return)
        #undef M
        };
    }

#endif
    return kBaseline;
}

#ifndef SK_JUMPER_DISABLE_8BIT
    static const SkJumper_Engine kNone = {
    #define M(stage) nullptr,
        { SK_RASTER_PIPELINE_STAGES(M) },
    #undef M
        nullptr,
        nullptr,
    };
    static SkJumper_Engine gLowp = kNone;
    static SkOnce gChooseLowpOnce;

    static SkJumper_Engine choose_lowp() {
    #if !__has_feature(memory_sanitizer) && (defined(__x86_64__) || defined(_M_X64))
        if (1 && SkCpu::Supports(SkCpu::HSW)) {
            return {
            #define M(st) hsw_8bit<SkRasterPipeline::st>(),
                { SK_RASTER_PIPELINE_STAGES(M) },
                ASM(start_pipeline,hsw_8bit),
                ASM(just_return   ,hsw_8bit)
            #undef M
            };
        }
        if (1 && SkCpu::Supports(SkCpu::SSE41)) {
            return {
            #define M(st) sse41_8bit<SkRasterPipeline::st>(),
                { SK_RASTER_PIPELINE_STAGES(M) },
                ASM(start_pipeline,sse41_8bit),
                ASM(just_return   ,sse41_8bit)
            #undef M
            };
        }
        if (1 && SkCpu::Supports(SkCpu::SSE2)) {
            return {
            #define M(st) sse2_8bit<SkRasterPipeline::st>(),
                { SK_RASTER_PIPELINE_STAGES(M) },
                ASM(start_pipeline,sse2_8bit),
                ASM(just_return   ,sse2_8bit)
            #undef M
            };
        }
    #elif !defined(SK_JUMPER_LEGACY_X86_8BIT) && \
        (defined(__i386__) || defined(_M_IX86))
        if (1 && SkCpu::Supports(SkCpu::SSE2)) {
            return {
            #define M(st) sse2_8bit<SkRasterPipeline::st>(),
                { SK_RASTER_PIPELINE_STAGES(M) },
                ASM(start_pipeline,sse2_8bit),
                ASM(just_return   ,sse2_8bit)
            #undef M
            };
        }

    #elif defined(JUMPER_HAS_NEON_8BIT)
        return {
        #define M(st) neon_8bit<SkRasterPipeline::st>(),
            { SK_RASTER_PIPELINE_STAGES(M) },
            sk_start_pipeline_8bit,
            sk_just_return_8bit,
        #undef M
        };
    #endif
        return kNone;
    }
#endif

const SkJumper_Engine& SkRasterPipeline::build_pipeline(void** ip) const {
#ifndef SK_JUMPER_DISABLE_8BIT
    gChooseLowpOnce([]{ gLowp = choose_lowp(); });

    // First try to build a lowp pipeline.  If that fails, fall back to normal float gEngine.
    void** reset_point = ip;
    *--ip = (void*)gLowp.just_return;
    for (const StageList* st = fStages; st; st = st->prev) {
        if (st->stage == SkRasterPipeline::clamp_0 ||
            st->stage == SkRasterPipeline::clamp_1) {
            continue;  // No-ops in lowp.
        }
        if (StageFn* fn = gLowp.stages[st->stage]) {
            if (st->ctx) {
                *--ip = st->ctx;
            }
            *--ip = (void*)fn;
        } else {
            log_missing(st->stage);
            ip = reset_point;
            break;
        }
    }
    if (ip != reset_point) {
        return gLowp;
    }
#endif

    gChooseEngineOnce([]{ gEngine = choose_engine(); });
    // We're building the pipeline backwards, so we start with the final stage just_return.
    *--ip = (void*)gEngine.just_return;

    // Still going backwards, each stage's context pointer then its StageFn.
    for (const StageList* st = fStages; st; st = st->prev) {
        if (st->ctx) {
            *--ip = st->ctx;
        }
        *--ip = (void*)gEngine.stages[st->stage];
    }
    return gEngine;
}

void SkRasterPipeline::run(size_t x, size_t y, size_t w, size_t h) const {
    if (this->empty()) {
        return;
    }

    // Best to not use fAlloc here... we can't bound how often run() will be called.
    SkAutoSTMalloc<64, void*> program(fSlotsNeeded);

    const SkJumper_Engine& engine = this->build_pipeline(program.get() + fSlotsNeeded);
    engine.start_pipeline(x,y,x+w,y+h, program.get());
}

std::function<void(size_t, size_t, size_t, size_t)> SkRasterPipeline::compile() const {
    if (this->empty()) {
        return [](size_t, size_t, size_t, size_t) {};
    }

    void** program = fAlloc->makeArray<void*>(fSlotsNeeded);
    const SkJumper_Engine& engine = this->build_pipeline(program + fSlotsNeeded);

    auto start_pipeline = engine.start_pipeline;
    return [=](size_t x, size_t y, size_t w, size_t h) {
        start_pipeline(x,y,x+w,y+h, program);
    };
}

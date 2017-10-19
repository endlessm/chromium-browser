/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkRasterPipeline.h"
#include "SkPM4f.h"
#include "SkPM4fPriv.h"
#include "../jumper/SkJumper.h"

SkRasterPipeline::SkRasterPipeline(SkArenaAlloc* alloc) : fAlloc(alloc) {
    this->reset();
}
void SkRasterPipeline::reset() {
    fStages      = nullptr;
    fNumStages   = 0;
    fSlotsNeeded = 1;  // We always need one extra slot for just_return().
    fClamped     = true;
}

void SkRasterPipeline::append(StockStage stage, void* ctx) {
    SkASSERT(stage != from_srgb);      // Please use append_from_srgb().
    SkASSERT(stage != uniform_color);  // Please use append_constant_color().
    this->unchecked_append(stage, ctx);
}
void SkRasterPipeline::unchecked_append(StockStage stage, void* ctx) {
    fStages = fAlloc->make<StageList>( StageList{fStages, stage, ctx} );
    fNumStages   += 1;
    fSlotsNeeded += ctx ? 2 : 1;
}

void SkRasterPipeline::extend(const SkRasterPipeline& src) {
    if (src.empty()) {
        return;
    }
    auto stages = fAlloc->makeArrayDefault<StageList>(src.fNumStages);

    int n = src.fNumStages;
    const StageList* st = src.fStages;
    while (n --> 1) {
        stages[n]      = *st;
        stages[n].prev = &stages[n-1];
        st = st->prev;
    }
    stages[0]      = *st;
    stages[0].prev = fStages;

    fStages = &stages[src.fNumStages - 1];
    fNumStages   += src.fNumStages;
    fSlotsNeeded += src.fSlotsNeeded - 1;  // Don't double count just_returns().
    fClamped = fClamped && src.fClamped;
}

void SkRasterPipeline::dump() const {
    SkDebugf("SkRasterPipeline, %d stages (in reverse)\n", fNumStages);
    for (auto st = fStages; st; st = st->prev) {
        const char* name = "";
        switch (st->stage) {
        #define M(x) case x: name = #x; break;
            SK_RASTER_PIPELINE_STAGES(M)
        #undef M
        }
        SkDebugf("\t%s\n", name);
    }
    SkDebugf("\n");
}

//#define TRACK_COLOR_HISTOGRAM
#ifdef TRACK_COLOR_HISTOGRAM
    static int gBlack;
    static int gWhite;
    static int gColor;
    #define INC_BLACK   gBlack++
    #define INC_WHITE   gWhite++
    #define INC_COLOR   gColor++
#else
    #define INC_BLACK
    #define INC_WHITE
    #define INC_COLOR
#endif

void SkRasterPipeline::append_constant_color(SkArenaAlloc* alloc, const float rgba[4]) {
    if (rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 0 && rgba[3] == 1) {
        this->append(black_color);
        INC_BLACK;
    } else if (rgba[0] == 1 && rgba[1] == 1 && rgba[2] == 1 && rgba[3] == 1) {
        this->append(white_color);
        INC_WHITE;
    } else {
        auto ctx = alloc->make<SkJumper_UniformColorCtx>();
        Sk4f color = Sk4f::Load(rgba);
        color.store(&ctx->r);
        ctx->rgba = Sk4f_toL32(color);

        this->unchecked_append(uniform_color, ctx);
        INC_COLOR;
    }

#ifdef TRACK_COLOR_HISTOGRAM
    SkDebugf("B=%d W=%d C=%d\n", gBlack, gWhite, gColor);
#endif
}

#undef INC_BLACK
#undef INC_WHITE
#undef INC_COLOR

// It's pretty easy to start with sound premultiplied linear floats, pack those
// to sRGB encoded bytes, then read them back to linear floats and find them not
// quite premultiplied, with a color channel just a smidge greater than the alpha
// channel.  This can happen basically any time we have different transfer
// functions for alpha and colors... sRGB being the only one we draw into.

// This is an annoying problem with no known good solution.  So apply the clamp hammer.

void SkRasterPipeline::append_from_srgb(SkAlphaType at) {
    this->unchecked_append(from_srgb, nullptr);
    if (at == kPremul_SkAlphaType) {
        this->append(SkRasterPipeline::clamp_a);
    }
}

void SkRasterPipeline::append_from_srgb_dst(SkAlphaType at) {
    this->unchecked_append(from_srgb_dst, nullptr);
    if (at == kPremul_SkAlphaType) {
        this->append(SkRasterPipeline::clamp_a_dst);
    }
}

//static int gCounts[5] = { 0, 0, 0, 0, 0 };

void SkRasterPipeline::append_matrix(SkArenaAlloc* alloc, const SkMatrix& matrix) {
    SkMatrix::TypeMask mt = matrix.getType();
#if 0
    if (mt > 4) mt = 4;
    gCounts[mt] += 1;
    SkDebugf("matrices: %d %d %d %d %d\n",
             gCounts[0], gCounts[1], gCounts[2], gCounts[3], gCounts[4]);
#endif

    // Based on a histogram of skps, we determined the following special cases were common, more
    // or fewer can be used if client behaviors change.

    if (mt == SkMatrix::kIdentity_Mask) {
        return;
    }
    if (mt == SkMatrix::kTranslate_Mask) {
        float* trans = alloc->makeArrayDefault<float>(2);
        trans[0] = matrix.getTranslateX();
        trans[1] = matrix.getTranslateY();
        this->append(SkRasterPipeline::matrix_translate, trans);
    } else if ((mt | (SkMatrix::kScale_Mask | SkMatrix::kTranslate_Mask)) ==
                     (SkMatrix::kScale_Mask | SkMatrix::kTranslate_Mask)) {
        float* scaleTrans = alloc->makeArrayDefault<float>(4);
        scaleTrans[0] = matrix.getTranslateX();
        scaleTrans[1] = matrix.getTranslateY();
        scaleTrans[2] = matrix.getScaleX();
        scaleTrans[3] = matrix.getScaleY();
        this->append(SkRasterPipeline::matrix_scale_translate, scaleTrans);
    } else {
        float* storage = alloc->makeArrayDefault<float>(9);
        if (matrix.asAffine(storage)) {
            // note: asAffine and the 2x3 stage really only need 6 entries
            this->append(SkRasterPipeline::matrix_2x3, storage);
        } else {
            matrix.get9(storage);
            this->append(SkRasterPipeline::matrix_perspective, storage);
        }
    }
}

void SkRasterPipeline::clamp_if_unclamped(SkAlphaType alphaType) {
    if (!fClamped) {
        this->append(SkRasterPipeline::clamp_0);
        this->append(alphaType == kPremul_SkAlphaType ? SkRasterPipeline::clamp_a
                                                      : SkRasterPipeline::clamp_1);
        fClamped = true;
    }
}

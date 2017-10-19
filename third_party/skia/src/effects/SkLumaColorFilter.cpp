/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkLumaColorFilter.h"
#include "SkPM4f.h"
#include "SkColorPriv.h"
#include "SkRasterPipeline.h"
#include "SkString.h"

#if SK_SUPPORT_GPU
#include "GrContext.h"
#include "glsl/GrGLSLFragmentProcessor.h"
#include "glsl/GrGLSLFragmentShaderBuilder.h"
#endif

void SkLumaColorFilter::onAppendStages(SkRasterPipeline* p,
                                       SkColorSpace* dst,
                                       SkArenaAlloc* scratch,
                                       bool shaderIsOpaque) const {
    p->append(SkRasterPipeline::luminance_to_alpha);
}

sk_sp<SkColorFilter> SkLumaColorFilter::Make() {
    return sk_sp<SkColorFilter>(new SkLumaColorFilter);
}

SkLumaColorFilter::SkLumaColorFilter() : INHERITED() {}

sk_sp<SkFlattenable> SkLumaColorFilter::CreateProc(SkReadBuffer&) {
    return Make();
}

void SkLumaColorFilter::flatten(SkWriteBuffer&) const {}

#ifndef SK_IGNORE_TO_STRING
void SkLumaColorFilter::toString(SkString* str) const {
    str->append("SkLumaColorFilter ");
}
#endif

#if SK_SUPPORT_GPU
class LumaColorFilterEffect : public GrFragmentProcessor {
public:
    static std::unique_ptr<GrFragmentProcessor> Make() {
        return std::unique_ptr<GrFragmentProcessor>(new LumaColorFilterEffect);
    }

    const char* name() const override { return "Luminance-to-Alpha"; }

    std::unique_ptr<GrFragmentProcessor> clone() const override { return Make(); }

private:
    class GLSLProcessor : public GrGLSLFragmentProcessor {
    public:
        static void GenKey(const GrProcessor&, const GrShaderCaps&, GrProcessorKeyBuilder*) {}

        void emitCode(EmitArgs& args) override {
            if (nullptr == args.fInputColor) {
                args.fInputColor = "float4(1)";
            }

            GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
            fragBuilder->codeAppendf("\tfloat luma = dot(float3(%f, %f, %f), %s.rgb);\n",
                                     SK_ITU_BT709_LUM_COEFF_R,
                                     SK_ITU_BT709_LUM_COEFF_G,
                                     SK_ITU_BT709_LUM_COEFF_B,
                                     args.fInputColor);
            fragBuilder->codeAppendf("\t%s = float4(0, 0, 0, luma);\n",
                                     args.fOutputColor);

        }

    private:
        typedef GrGLSLFragmentProcessor INHERITED;
    };

    LumaColorFilterEffect() : INHERITED(kConstantOutputForConstantInput_OptimizationFlag) {
        this->initClassID<LumaColorFilterEffect>();
    }

    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override {
        return new GLSLProcessor;
    }

    virtual void onGetGLSLProcessorKey(const GrShaderCaps& caps,
                                       GrProcessorKeyBuilder* b) const override {
        GLSLProcessor::GenKey(*this, caps, b);
    }

    bool onIsEqual(const GrFragmentProcessor&) const override { return true; }

    GrColor4f constantOutputForConstantInput(GrColor4f input) const override {
        float luma = SK_ITU_BT709_LUM_COEFF_R * input.fRGBA[0] +
                     SK_ITU_BT709_LUM_COEFF_G * input.fRGBA[1] +
                     SK_ITU_BT709_LUM_COEFF_B * input.fRGBA[2];
        return GrColor4f(0, 0, 0, luma);
    }

    typedef GrFragmentProcessor INHERITED;
};

std::unique_ptr<GrFragmentProcessor> SkLumaColorFilter::asFragmentProcessor(GrContext*,
                                                                            SkColorSpace*) const {
    return LumaColorFilterEffect::Make();
}
#endif

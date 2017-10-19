/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_CONTEXT
#define SKSL_CONTEXT

#include "ir/SkSLType.h"
#include "ir/SkSLExpression.h"

namespace SkSL {

/**
 * Contains compiler-wide objects, which currently means the core types.
 */
class Context {
public:
    Context()
    : fInvalid_Type(new Type(String("<INVALID>")))
    , fVoid_Type(new Type(String("void")))
    , fDouble_Type(new Type(String("double"), Type::kFloat_NumberKind))
    , fDouble2_Type(new Type(String("double2"), *fDouble_Type, 2))
    , fDouble3_Type(new Type(String("double3"), *fDouble_Type, 3))
    , fDouble4_Type(new Type(String("double4"), *fDouble_Type, 4))
    , fFloat_Type(new Type(String("float"), Type::kFloat_NumberKind))
    , fFloat2_Type(new Type(String("float2"), *fFloat_Type, 2))
    , fFloat3_Type(new Type(String("float3"), *fFloat_Type, 3))
    , fFloat4_Type(new Type(String("float4"), *fFloat_Type, 4))
    , fHalf_Type(new Type(String("half"), Type::kFloat_NumberKind))
    , fHalf2_Type(new Type(String("half2"), *fHalf_Type, 2))
    , fHalf3_Type(new Type(String("half3"), *fHalf_Type, 3))
    , fHalf4_Type(new Type(String("half4"), *fHalf_Type, 4))
    , fUInt_Type(new Type(String("uint"), Type::kUnsigned_NumberKind))
    , fUInt2_Type(new Type(String("uint2"), *fUInt_Type, 2))
    , fUInt3_Type(new Type(String("uint3"), *fUInt_Type, 3))
    , fUInt4_Type(new Type(String("uint4"), *fUInt_Type, 4))
    , fInt_Type(new Type(String("int"), Type::kSigned_NumberKind))
    , fInt2_Type(new Type(String("int2"), *fInt_Type, 2))
    , fInt3_Type(new Type(String("int3"), *fInt_Type, 3))
    , fInt4_Type(new Type(String("int4"), *fInt_Type, 4))
    , fUShort_Type(new Type(String("ushort"), Type::kUnsigned_NumberKind))
    , fUShort2_Type(new Type(String("ushort2"), *fUShort_Type, 2))
    , fUShort3_Type(new Type(String("ushort3"), *fUShort_Type, 3))
    , fUShort4_Type(new Type(String("ushort4"), *fUShort_Type, 4))
    , fShort_Type(new Type(String("short"), Type::kSigned_NumberKind))
    , fShort2_Type(new Type(String("short2"), *fShort_Type, 2))
    , fShort3_Type(new Type(String("short3"), *fShort_Type, 3))
    , fShort4_Type(new Type(String("short4"), *fShort_Type, 4))
    , fBool_Type(new Type(String("bool"), Type::kNonnumeric_NumberKind))
    , fBool2_Type(new Type(String("bool2"), *fBool_Type, 2))
    , fBool3_Type(new Type(String("bool3"), *fBool_Type, 3))
    , fBool4_Type(new Type(String("bool4"), *fBool_Type, 4))
    , fFloat2x2_Type(new Type(String("float2x2"), *fFloat_Type, 2, 2))
    , fFloat2x3_Type(new Type(String("float2x3"), *fFloat_Type, 2, 3))
    , fFloat2x4_Type(new Type(String("float2x4"), *fFloat_Type, 2, 4))
    , fFloat3x2_Type(new Type(String("float3x2"), *fFloat_Type, 3, 2))
    , fFloat3x3_Type(new Type(String("float3x3"), *fFloat_Type, 3, 3))
    , fFloat3x4_Type(new Type(String("float3x4"), *fFloat_Type, 3, 4))
    , fFloat4x2_Type(new Type(String("float4x2"), *fFloat_Type, 4, 2))
    , fFloat4x3_Type(new Type(String("float4x3"), *fFloat_Type, 4, 3))
    , fFloat4x4_Type(new Type(String("float4x4"), *fFloat_Type, 4, 4))
    , fHalf2x2_Type(new Type(String("half2x2"), *fHalf_Type, 2, 2))
    , fHalf2x3_Type(new Type(String("half2x3"), *fHalf_Type, 2, 3))
    , fHalf2x4_Type(new Type(String("half2x4"), *fHalf_Type, 2, 4))
    , fHalf3x2_Type(new Type(String("half3x2"), *fHalf_Type, 3, 2))
    , fHalf3x3_Type(new Type(String("half3x3"), *fHalf_Type, 3, 3))
    , fHalf3x4_Type(new Type(String("half3x4"), *fHalf_Type, 3, 4))
    , fHalf4x2_Type(new Type(String("half4x2"), *fHalf_Type, 4, 2))
    , fHalf4x3_Type(new Type(String("half4x3"), *fHalf_Type, 4, 3))
    , fHalf4x4_Type(new Type(String("half4x4"), *fHalf_Type, 4, 4))
    , fDouble2x2_Type(new Type(String("double2x2"), *fDouble_Type, 2, 2))
    , fDouble2x3_Type(new Type(String("double2x3"), *fDouble_Type, 2, 3))
    , fDouble2x4_Type(new Type(String("double2x4"), *fDouble_Type, 2, 4))
    , fDouble3x2_Type(new Type(String("double3x2"), *fDouble_Type, 3, 2))
    , fDouble3x3_Type(new Type(String("double3x3"), *fDouble_Type, 3, 3))
    , fDouble3x4_Type(new Type(String("double3x4"), *fDouble_Type, 3, 4))
    , fDouble4x2_Type(new Type(String("double4x2"), *fDouble_Type, 4, 2))
    , fDouble4x3_Type(new Type(String("double4x3"), *fDouble_Type, 4, 3))
    , fDouble4x4_Type(new Type(String("double4x4"), *fDouble_Type, 4, 4))
    , fSampler1D_Type(new Type(String("sampler1D"), SpvDim1D, false, false, false, true))
    , fSampler2D_Type(new Type(String("sampler2D"), SpvDim2D, false, false, false, true))
    , fSampler3D_Type(new Type(String("sampler3D"), SpvDim3D, false, false, false, true))
    , fSamplerExternalOES_Type(new Type(String("samplerExternalOES"), SpvDim2D, false, false,
                                        false, true))
    , fSamplerCube_Type(new Type(String("samplerCube"), SpvDimCube, false, false, false, true))
    , fSampler2DRect_Type(new Type(String("sampler2DRect"), SpvDimRect, false, false, false, true))
    , fSampler1DArray_Type(new Type(String("sampler1DArray")))
    , fSampler2DArray_Type(new Type(String("sampler2DArray")))
    , fSamplerCubeArray_Type(new Type(String("samplerCubeArray")))
    , fSamplerBuffer_Type(new Type(String("samplerBuffer"), SpvDimBuffer, false, false, false,
                                   true))
    , fSampler2DMS_Type(new Type(String("sampler2DMS")))
    , fSampler2DMSArray_Type(new Type(String("sampler2DMSArray")))
    , fSampler1DShadow_Type(new Type(String("sampler1DShadow")))
    , fSampler2DShadow_Type(new Type(String("sampler2DShadow")))
    , fSamplerCubeShadow_Type(new Type(String("samplerCubeShadow")))
    , fSampler2DRectShadow_Type(new Type(String("sampler2DRectShadow")))
    , fSampler1DArrayShadow_Type(new Type(String("sampler1DArrayShadow")))
    , fSampler2DArrayShadow_Type(new Type(String("sampler2DArrayShadow")))
    , fSamplerCubeArrayShadow_Type(new Type(String("samplerCubeArrayShadow")))

    // Related to below FIXME, gsampler*s don't currently expand to cover integer case.
    , fISampler2D_Type(new Type(String("isampler2D"), SpvDim2D, false, false, false, true))

    // FIXME express these as "gimage2D" that expand to image2D, iimage2D, and uimage2D.
    , fImage2D_Type(new Type(String("image2D"), SpvDim2D, false, false, false, true))
    , fIImage2D_Type(new Type(String("iimage2D"), SpvDim2D, false, false, false, true))

    // FIXME express these as "gsubpassInput" that expand to subpassInput, isubpassInput,
    // and usubpassInput.
    , fSubpassInput_Type(new Type(String("subpassInput"), SpvDimSubpassData, false, false,
                                           false, false))
    , fSubpassInputMS_Type(new Type(String("subpassInputMS"), SpvDimSubpassData, false, false,
                                             true, false))

    // FIXME figure out what we're supposed to do with the gsampler et al. types)
    , fGSampler1D_Type(new Type(String("$gsampler1D"), static_type(*fSampler1D_Type)))
    , fGSampler2D_Type(new Type(String("$gsampler2D"), static_type(*fSampler2D_Type)))
    , fGSampler3D_Type(new Type(String("$gsampler3D"), static_type(*fSampler3D_Type)))
    , fGSamplerCube_Type(new Type(String("$gsamplerCube"), static_type(*fSamplerCube_Type)))
    , fGSampler2DRect_Type(new Type(String("$gsampler2DRect"), static_type(*fSampler2DRect_Type)))
    , fGSampler1DArray_Type(new Type(String("$gsampler1DArray"),
                                     static_type(*fSampler1DArray_Type)))
    , fGSampler2DArray_Type(new Type(String("$gsampler2DArray"),
                                     static_type(*fSampler2DArray_Type)))
    , fGSamplerCubeArray_Type(new Type(String("$gsamplerCubeArray"),
                                       static_type(*fSamplerCubeArray_Type)))
    , fGSamplerBuffer_Type(new Type(String("$gsamplerBuffer"), static_type(*fSamplerBuffer_Type)))
    , fGSampler2DMS_Type(new Type(String("$gsampler2DMS"), static_type(*fSampler2DMS_Type)))
    , fGSampler2DMSArray_Type(new Type(String("$gsampler2DMSArray"),
                                       static_type(*fSampler2DMSArray_Type)))
    , fGSampler2DArrayShadow_Type(new Type(String("$gsampler2DArrayShadow"),
                                           static_type(*fSampler2DArrayShadow_Type)))
    , fGSamplerCubeArrayShadow_Type(new Type(String("$gsamplerCubeArrayShadow"),
                                             static_type(*fSamplerCubeArrayShadow_Type)))
    , fGenType_Type(new Type(String("$genType"), { fFloat_Type.get(), fFloat2_Type.get(),
                                                   fFloat3_Type.get(), fFloat4_Type.get() }))
    , fGenHType_Type(new Type(String("$genHType"), { fHalf_Type.get(), fHalf2_Type.get(),
                                                     fHalf3_Type.get(), fHalf4_Type.get() }))
    , fGenDType_Type(new Type(String("$genDType"), { fDouble_Type.get(), fDouble2_Type.get(),
                                                     fDouble3_Type.get(), fDouble4_Type.get() }))
    , fGenIType_Type(new Type(String("$genIType"), { fInt_Type.get(), fInt2_Type.get(),
                                                     fInt3_Type.get(), fInt4_Type.get() }))
    , fGenUType_Type(new Type(String("$genUType"), { fUInt_Type.get(), fUInt2_Type.get(),
                                                     fUInt3_Type.get(), fUInt4_Type.get() }))
    , fGenBType_Type(new Type(String("$genBType"), { fBool_Type.get(), fBool2_Type.get(),
                                                     fBool3_Type.get(), fBool4_Type.get() }))
    , fMat_Type(new Type(String("$mat"), { fFloat2x2_Type.get(),  fFloat2x3_Type.get(),
                                           fFloat2x4_Type.get(),  fFloat3x2_Type.get(),
                                           fFloat3x3_Type.get(),  fFloat3x4_Type.get(),
                                           fFloat4x2_Type.get(),  fFloat4x3_Type.get(),
                                           fFloat4x4_Type.get(),  fHalf2x2_Type.get(),
                                           fHalf2x3_Type.get(),   fHalf2x4_Type.get(),
                                           fHalf3x2_Type.get(),   fHalf3x3_Type.get(),
                                           fHalf3x4_Type.get(),   fHalf4x2_Type.get(),
                                           fHalf4x3_Type.get(),   fHalf4x4_Type.get(),
                                           fDouble2x2_Type.get(), fDouble2x3_Type.get(),
                                           fDouble2x4_Type.get(), fDouble3x2_Type.get(),
                                           fDouble3x3_Type.get(), fDouble3x4_Type.get(),
                                           fDouble4x2_Type.get(), fDouble4x3_Type.get(),
                                           fDouble4x4_Type.get() }))
    , fVec_Type(new Type(String("$vec"), { fInvalid_Type.get(), fFloat2_Type.get(),
                                           fFloat3_Type.get(), fFloat4_Type.get() }))
    , fGVec_Type(new Type(String("$gvec")))
    , fGVec2_Type(new Type(String("$gfloat2")))
    , fGVec3_Type(new Type(String("$gfloat3")))
    , fGVec4_Type(new Type(String("$gfloat4"), static_type(*fFloat4_Type)))
    , fHVec_Type(new Type(String("$hvec"), { fInvalid_Type.get(), fHalf2_Type.get(),
                                             fHalf3_Type.get(), fHalf4_Type.get() }))
    , fDVec_Type(new Type(String("$dvec"), { fInvalid_Type.get(), fDouble2_Type.get(),
                                             fDouble3_Type.get(), fDouble4_Type.get() }))
    , fIVec_Type(new Type(String("$ivec"), { fInvalid_Type.get(), fInt2_Type.get(),
                                             fInt3_Type.get(), fInt4_Type.get() }))
    , fUVec_Type(new Type(String("$uvec"), { fInvalid_Type.get(), fUInt2_Type.get(),
                                             fUInt3_Type.get(), fUInt4_Type.get() }))
    , fSVec_Type(new Type(String("$svec"), { fInvalid_Type.get(), fShort2_Type.get(),
                                             fShort3_Type.get(), fShort4_Type.get() }))
    , fUSVec_Type(new Type(String("$usvec"), { fInvalid_Type.get(), fUShort2_Type.get(),
                                               fUShort3_Type.get(), fUShort4_Type.get() }))
    , fBVec_Type(new Type(String("$bvec"), { fInvalid_Type.get(), fBool2_Type.get(),
                                             fBool3_Type.get(), fBool4_Type.get() }))
    , fSkCaps_Type(new Type(String("$sk_Caps")))
    , fSkArgs_Type(new Type(String("$sk_Args")))
    , fColorSpaceXform_Type(new Type(String("colorSpaceXform"), *fFloat_Type, 4, 4))
    , fDefined_Expression(new Defined(*fInvalid_Type)) {}

    static std::vector<const Type*> static_type(const Type& t) {
        return { &t, &t, &t, &t };
    }

    const std::unique_ptr<Type> fInvalid_Type;
    const std::unique_ptr<Type> fVoid_Type;

    const std::unique_ptr<Type> fDouble_Type;
    const std::unique_ptr<Type> fDouble2_Type;
    const std::unique_ptr<Type> fDouble3_Type;
    const std::unique_ptr<Type> fDouble4_Type;

    const std::unique_ptr<Type> fFloat_Type;
    const std::unique_ptr<Type> fFloat2_Type;
    const std::unique_ptr<Type> fFloat3_Type;
    const std::unique_ptr<Type> fFloat4_Type;

    const std::unique_ptr<Type> fHalf_Type;
    const std::unique_ptr<Type> fHalf2_Type;
    const std::unique_ptr<Type> fHalf3_Type;
    const std::unique_ptr<Type> fHalf4_Type;

    const std::unique_ptr<Type> fUInt_Type;
    const std::unique_ptr<Type> fUInt2_Type;
    const std::unique_ptr<Type> fUInt3_Type;
    const std::unique_ptr<Type> fUInt4_Type;

    const std::unique_ptr<Type> fInt_Type;
    const std::unique_ptr<Type> fInt2_Type;
    const std::unique_ptr<Type> fInt3_Type;
    const std::unique_ptr<Type> fInt4_Type;

    const std::unique_ptr<Type> fUShort_Type;
    const std::unique_ptr<Type> fUShort2_Type;
    const std::unique_ptr<Type> fUShort3_Type;
    const std::unique_ptr<Type> fUShort4_Type;

    const std::unique_ptr<Type> fShort_Type;
    const std::unique_ptr<Type> fShort2_Type;
    const std::unique_ptr<Type> fShort3_Type;
    const std::unique_ptr<Type> fShort4_Type;

    const std::unique_ptr<Type> fBool_Type;
    const std::unique_ptr<Type> fBool2_Type;
    const std::unique_ptr<Type> fBool3_Type;
    const std::unique_ptr<Type> fBool4_Type;

    const std::unique_ptr<Type> fFloat2x2_Type;
    const std::unique_ptr<Type> fFloat2x3_Type;
    const std::unique_ptr<Type> fFloat2x4_Type;
    const std::unique_ptr<Type> fFloat3x2_Type;
    const std::unique_ptr<Type> fFloat3x3_Type;
    const std::unique_ptr<Type> fFloat3x4_Type;
    const std::unique_ptr<Type> fFloat4x2_Type;
    const std::unique_ptr<Type> fFloat4x3_Type;
    const std::unique_ptr<Type> fFloat4x4_Type;

    const std::unique_ptr<Type> fHalf2x2_Type;
    const std::unique_ptr<Type> fHalf2x3_Type;
    const std::unique_ptr<Type> fHalf2x4_Type;
    const std::unique_ptr<Type> fHalf3x2_Type;
    const std::unique_ptr<Type> fHalf3x3_Type;
    const std::unique_ptr<Type> fHalf3x4_Type;
    const std::unique_ptr<Type> fHalf4x2_Type;
    const std::unique_ptr<Type> fHalf4x3_Type;
    const std::unique_ptr<Type> fHalf4x4_Type;

    const std::unique_ptr<Type> fDouble2x2_Type;
    const std::unique_ptr<Type> fDouble2x3_Type;
    const std::unique_ptr<Type> fDouble2x4_Type;
    const std::unique_ptr<Type> fDouble3x2_Type;
    const std::unique_ptr<Type> fDouble3x3_Type;
    const std::unique_ptr<Type> fDouble3x4_Type;
    const std::unique_ptr<Type> fDouble4x2_Type;
    const std::unique_ptr<Type> fDouble4x3_Type;
    const std::unique_ptr<Type> fDouble4x4_Type;

    const std::unique_ptr<Type> fSampler1D_Type;
    const std::unique_ptr<Type> fSampler2D_Type;
    const std::unique_ptr<Type> fSampler3D_Type;
    const std::unique_ptr<Type> fSamplerExternalOES_Type;
    const std::unique_ptr<Type> fSamplerCube_Type;
    const std::unique_ptr<Type> fSampler2DRect_Type;
    const std::unique_ptr<Type> fSampler1DArray_Type;
    const std::unique_ptr<Type> fSampler2DArray_Type;
    const std::unique_ptr<Type> fSamplerCubeArray_Type;
    const std::unique_ptr<Type> fSamplerBuffer_Type;
    const std::unique_ptr<Type> fSampler2DMS_Type;
    const std::unique_ptr<Type> fSampler2DMSArray_Type;
    const std::unique_ptr<Type> fSampler1DShadow_Type;
    const std::unique_ptr<Type> fSampler2DShadow_Type;
    const std::unique_ptr<Type> fSamplerCubeShadow_Type;
    const std::unique_ptr<Type> fSampler2DRectShadow_Type;
    const std::unique_ptr<Type> fSampler1DArrayShadow_Type;
    const std::unique_ptr<Type> fSampler2DArrayShadow_Type;
    const std::unique_ptr<Type> fSamplerCubeArrayShadow_Type;

    const std::unique_ptr<Type> fISampler2D_Type;

    const std::unique_ptr<Type> fImage2D_Type;
    const std::unique_ptr<Type> fIImage2D_Type;

    const std::unique_ptr<Type> fSubpassInput_Type;
    const std::unique_ptr<Type> fSubpassInputMS_Type;

    const std::unique_ptr<Type> fGSampler1D_Type;
    const std::unique_ptr<Type> fGSampler2D_Type;
    const std::unique_ptr<Type> fGSampler3D_Type;
    const std::unique_ptr<Type> fGSamplerCube_Type;
    const std::unique_ptr<Type> fGSampler2DRect_Type;
    const std::unique_ptr<Type> fGSampler1DArray_Type;
    const std::unique_ptr<Type> fGSampler2DArray_Type;
    const std::unique_ptr<Type> fGSamplerCubeArray_Type;
    const std::unique_ptr<Type> fGSamplerBuffer_Type;
    const std::unique_ptr<Type> fGSampler2DMS_Type;
    const std::unique_ptr<Type> fGSampler2DMSArray_Type;
    const std::unique_ptr<Type> fGSampler2DArrayShadow_Type;
    const std::unique_ptr<Type> fGSamplerCubeArrayShadow_Type;

    const std::unique_ptr<Type> fGenType_Type;
    const std::unique_ptr<Type> fGenHType_Type;
    const std::unique_ptr<Type> fGenDType_Type;
    const std::unique_ptr<Type> fGenIType_Type;
    const std::unique_ptr<Type> fGenUType_Type;
    const std::unique_ptr<Type> fGenBType_Type;

    const std::unique_ptr<Type> fMat_Type;

    const std::unique_ptr<Type> fVec_Type;

    const std::unique_ptr<Type> fGVec_Type;
    const std::unique_ptr<Type> fGVec2_Type;
    const std::unique_ptr<Type> fGVec3_Type;
    const std::unique_ptr<Type> fGVec4_Type;
    const std::unique_ptr<Type> fHVec_Type;
    const std::unique_ptr<Type> fDVec_Type;
    const std::unique_ptr<Type> fIVec_Type;
    const std::unique_ptr<Type> fUVec_Type;
    const std::unique_ptr<Type> fSVec_Type;
    const std::unique_ptr<Type> fUSVec_Type;

    const std::unique_ptr<Type> fBVec_Type;

    const std::unique_ptr<Type> fSkCaps_Type;
    const std::unique_ptr<Type> fSkArgs_Type;
    const std::unique_ptr<Type> fColorSpaceXform_Type;

    // dummy expression used to mark that a variable has a value during dataflow analysis (when it
    // could have several different values, or the analyzer is otherwise unable to assign it a
    // specific expression)
    const std::unique_ptr<Expression> fDefined_Expression;

private:
    class Defined : public Expression {
    public:
        Defined(const Type& type)
        : INHERITED(Position(), kDefined_Kind, type) {}

        bool hasSideEffects() const override {
            return false;
        }

        String description() const override {
            return String("<defined>");
        }

        typedef Expression INHERITED;
    };
};

} // namespace

#endif

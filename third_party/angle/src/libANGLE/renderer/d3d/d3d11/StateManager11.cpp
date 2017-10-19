//
// Copyright (c) 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// StateManager11.cpp: Defines a class for caching D3D11 state

#include "libANGLE/renderer/d3d/d3d11/StateManager11.h"

#include "common/bitset_utils.h"
#include "common/utilities.h"
#include "libANGLE/Context.h"
#include "libANGLE/Query.h"
#include "libANGLE/VertexArray.h"
#include "libANGLE/renderer/d3d/TextureD3D.h"
#include "libANGLE/renderer/d3d/d3d11/Buffer11.h"
#include "libANGLE/renderer/d3d/d3d11/Framebuffer11.h"
#include "libANGLE/renderer/d3d/d3d11/IndexBuffer11.h"
#include "libANGLE/renderer/d3d/d3d11/RenderTarget11.h"
#include "libANGLE/renderer/d3d/d3d11/Renderer11.h"
#include "libANGLE/renderer/d3d/d3d11/ShaderExecutable11.h"
#include "libANGLE/renderer/d3d/d3d11/TextureStorage11.h"
#include "libANGLE/renderer/d3d/d3d11/VertexArray11.h"

namespace rx
{

namespace
{
bool ImageIndexConflictsWithSRV(const gl::ImageIndex &index, D3D11_SHADER_RESOURCE_VIEW_DESC desc)
{
    unsigned mipLevel   = index.mipIndex;
    GLint layerIndex    = index.layerIndex;
    GLenum type         = index.type;

    switch (desc.ViewDimension)
    {
        case D3D11_SRV_DIMENSION_TEXTURE2D:
        {
            bool allLevels         = (desc.Texture2D.MipLevels == std::numeric_limits<UINT>::max());
            unsigned int maxSrvMip = desc.Texture2D.MipLevels + desc.Texture2D.MostDetailedMip;
            maxSrvMip              = allLevels ? INT_MAX : maxSrvMip;

            unsigned mipMin = index.mipIndex;
            unsigned mipMax = (layerIndex == -1) ? INT_MAX : layerIndex;

            return type == GL_TEXTURE_2D &&
                   gl::RangeUI(mipMin, mipMax)
                       .intersects(gl::RangeUI(desc.Texture2D.MostDetailedMip, maxSrvMip));
        }

        case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        {
            bool allLevels = (desc.Texture2DArray.MipLevels == std::numeric_limits<UINT>::max());
            unsigned int maxSrvMip =
                desc.Texture2DArray.MipLevels + desc.Texture2DArray.MostDetailedMip;
            maxSrvMip = allLevels ? INT_MAX : maxSrvMip;

            unsigned maxSlice = desc.Texture2DArray.FirstArraySlice + desc.Texture2DArray.ArraySize;

            // Cube maps can be mapped to Texture2DArray SRVs
            return (type == GL_TEXTURE_2D_ARRAY || gl::IsCubeMapTextureTarget(type)) &&
                   desc.Texture2DArray.MostDetailedMip <= mipLevel && mipLevel < maxSrvMip &&
                   desc.Texture2DArray.FirstArraySlice <= static_cast<UINT>(layerIndex) &&
                   static_cast<UINT>(layerIndex) < maxSlice;
        }

        case D3D11_SRV_DIMENSION_TEXTURECUBE:
        {
            bool allLevels = (desc.TextureCube.MipLevels == std::numeric_limits<UINT>::max());
            unsigned int maxSrvMip = desc.TextureCube.MipLevels + desc.TextureCube.MostDetailedMip;
            maxSrvMip              = allLevels ? INT_MAX : maxSrvMip;

            return gl::IsCubeMapTextureTarget(type) &&
                   desc.TextureCube.MostDetailedMip <= mipLevel && mipLevel < maxSrvMip;
        }

        case D3D11_SRV_DIMENSION_TEXTURE3D:
        {
            bool allLevels         = (desc.Texture3D.MipLevels == std::numeric_limits<UINT>::max());
            unsigned int maxSrvMip = desc.Texture3D.MipLevels + desc.Texture3D.MostDetailedMip;
            maxSrvMip              = allLevels ? INT_MAX : maxSrvMip;

            return type == GL_TEXTURE_3D && desc.Texture3D.MostDetailedMip <= mipLevel &&
                   mipLevel < maxSrvMip;
        }
        default:
            // We only handle the cases corresponding to valid image indexes
            UNIMPLEMENTED();
    }

    return false;
}

// Does *not* increment the resource ref count!!
ID3D11Resource *GetViewResource(ID3D11View *view)
{
    ID3D11Resource *resource = nullptr;
    ASSERT(view);
    view->GetResource(&resource);
    resource->Release();
    return resource;
}

int GetWrapBits(GLenum wrap)
{
    switch (wrap)
    {
        case GL_CLAMP_TO_EDGE:
            return 0x1;
        case GL_REPEAT:
            return 0x2;
        case GL_MIRRORED_REPEAT:
            return 0x3;
        default:
            UNREACHABLE();
            return 0;
    }
}

Optional<size_t> FindFirstNonInstanced(
    const std::vector<const TranslatedAttribute *> &currentAttributes)
{
    for (size_t index = 0; index < currentAttributes.size(); ++index)
    {
        if (currentAttributes[index]->divisor == 0)
        {
            return Optional<size_t>(index);
        }
    }

    return Optional<size_t>::Invalid();
}

void SortAttributesByLayout(const gl::Program *program,
                            const std::vector<TranslatedAttribute> &vertexArrayAttribs,
                            const std::vector<TranslatedAttribute> &currentValueAttribs,
                            AttribIndexArray *sortedD3DSemanticsOut,
                            std::vector<const TranslatedAttribute *> *sortedAttributesOut)
{
    sortedAttributesOut->clear();

    const auto &locationToSemantic =
        GetImplAs<ProgramD3D>(program)->getAttribLocationToD3DSemantics();

    for (auto locationIndex : program->getActiveAttribLocationsMask())
    {
        int d3dSemantic = locationToSemantic[locationIndex];
        if (sortedAttributesOut->size() <= static_cast<size_t>(d3dSemantic))
        {
            sortedAttributesOut->resize(d3dSemantic + 1);
        }

        (*sortedD3DSemanticsOut)[d3dSemantic] = d3dSemantic;

        const auto *arrayAttrib = &vertexArrayAttribs[locationIndex];
        if (arrayAttrib->attribute && arrayAttrib->attribute->enabled)
        {
            (*sortedAttributesOut)[d3dSemantic] = arrayAttrib;
        }
        else
        {
            ASSERT(currentValueAttribs[locationIndex].attribute);
            (*sortedAttributesOut)[d3dSemantic] = &currentValueAttribs[locationIndex];
        }
    }
}

}  // anonymous namespace

// StateManager11::SRVCache Implementation.

void StateManager11::SRVCache::update(size_t resourceIndex, ID3D11ShaderResourceView *srv)
{
    ASSERT(resourceIndex < mCurrentSRVs.size());
    SRVRecord *record = &mCurrentSRVs[resourceIndex];

    record->srv = reinterpret_cast<uintptr_t>(srv);
    if (srv)
    {
        record->resource = reinterpret_cast<uintptr_t>(GetViewResource(srv));
        srv->GetDesc(&record->desc);
        mHighestUsedSRV = std::max(resourceIndex + 1, mHighestUsedSRV);
    }
    else
    {
        record->resource = 0;

        if (resourceIndex + 1 == mHighestUsedSRV)
        {
            do
            {
                --mHighestUsedSRV;
            } while (mHighestUsedSRV > 0 && mCurrentSRVs[mHighestUsedSRV].srv == 0);
        }
    }
}

void StateManager11::SRVCache::clear()
{
    if (mCurrentSRVs.empty())
    {
        return;
    }

    memset(&mCurrentSRVs[0], 0, sizeof(SRVRecord) * mCurrentSRVs.size());
    mHighestUsedSRV = 0;
}

// SamplerMetadataD3D11 implementation

SamplerMetadata11::SamplerMetadata11() : mDirty(true)
{
}

SamplerMetadata11::~SamplerMetadata11()
{
}

void SamplerMetadata11::initData(unsigned int samplerCount)
{
    mSamplerMetadata.resize(samplerCount);
}

void SamplerMetadata11::update(unsigned int samplerIndex, const gl::Texture &texture)
{
    unsigned int baseLevel = texture.getTextureState().getEffectiveBaseLevel();
    GLenum sizedFormat =
        texture.getFormat(texture.getTarget(), baseLevel).info->sizedInternalFormat;
    if (mSamplerMetadata[samplerIndex].baseLevel != static_cast<int>(baseLevel))
    {
        mSamplerMetadata[samplerIndex].baseLevel = static_cast<int>(baseLevel);
        mDirty                                   = true;
    }

    // Some metadata is needed only for integer textures. We avoid updating the constant buffer
    // unnecessarily by changing the data only in case the texture is an integer texture and
    // the values have changed.
    bool needIntegerTextureMetadata = false;
    // internalFormatBits == 0 means a 32-bit texture in the case of integer textures.
    int internalFormatBits = 0;
    switch (sizedFormat)
    {
        case GL_RGBA32I:
        case GL_RGBA32UI:
        case GL_RGB32I:
        case GL_RGB32UI:
        case GL_RG32I:
        case GL_RG32UI:
        case GL_R32I:
        case GL_R32UI:
            needIntegerTextureMetadata = true;
            break;
        case GL_RGBA16I:
        case GL_RGBA16UI:
        case GL_RGB16I:
        case GL_RGB16UI:
        case GL_RG16I:
        case GL_RG16UI:
        case GL_R16I:
        case GL_R16UI:
            needIntegerTextureMetadata = true;
            internalFormatBits         = 16;
            break;
        case GL_RGBA8I:
        case GL_RGBA8UI:
        case GL_RGB8I:
        case GL_RGB8UI:
        case GL_RG8I:
        case GL_RG8UI:
        case GL_R8I:
        case GL_R8UI:
            needIntegerTextureMetadata = true;
            internalFormatBits         = 8;
            break;
        case GL_RGB10_A2UI:
            needIntegerTextureMetadata = true;
            internalFormatBits         = 10;
            break;
        default:
            break;
    }
    if (needIntegerTextureMetadata)
    {
        if (mSamplerMetadata[samplerIndex].internalFormatBits != internalFormatBits)
        {
            mSamplerMetadata[samplerIndex].internalFormatBits = internalFormatBits;
            mDirty                                            = true;
        }
        // Pack the wrap values into one integer so we can fit all the metadata in one 4-integer
        // vector.
        GLenum wrapS  = texture.getWrapS();
        GLenum wrapT  = texture.getWrapT();
        GLenum wrapR  = texture.getWrapR();
        int wrapModes = GetWrapBits(wrapS) | (GetWrapBits(wrapT) << 2) | (GetWrapBits(wrapR) << 4);
        if (mSamplerMetadata[samplerIndex].wrapModes != wrapModes)
        {
            mSamplerMetadata[samplerIndex].wrapModes = wrapModes;
            mDirty                                   = true;
        }
    }
}

const SamplerMetadata11::dx_SamplerMetadata *SamplerMetadata11::getData() const
{
    return mSamplerMetadata.data();
}

size_t SamplerMetadata11::sizeBytes() const
{
    return sizeof(dx_SamplerMetadata) * mSamplerMetadata.size();
}

static const GLenum QueryTypes[] = {GL_ANY_SAMPLES_PASSED, GL_ANY_SAMPLES_PASSED_CONSERVATIVE,
                                    GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_TIME_ELAPSED_EXT,
                                    GL_COMMANDS_COMPLETED_CHROMIUM};

StateManager11::StateManager11(Renderer11 *renderer)
    : mRenderer(renderer),
      mInternalDirtyBits(),
      mCurBlendColor(0, 0, 0, 0),
      mCurSampleMask(0),
      mCurStencilRef(0),
      mCurStencilBackRef(0),
      mCurStencilSize(0),
      mCurScissorEnabled(false),
      mCurScissorRect(),
      mCurViewport(),
      mCurNear(0.0f),
      mCurFar(0.0f),
      mViewportBounds(),
      mCurPresentPathFastEnabled(false),
      mCurPresentPathFastColorBufferHeight(0),
      mDirtyCurrentValueAttribs(),
      mCurrentValueAttribs(),
      mCurrentInputLayout(),
      mInputLayoutIsDirty(false),
      mDirtyVertexBufferRange(gl::MAX_VERTEX_ATTRIBS, 0),
      mCurrentPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED),
      mAppliedIB(nullptr),
      mAppliedIBFormat(DXGI_FORMAT_UNKNOWN),
      mAppliedIBOffset(0),
      mAppliedIBChanged(false),
      mVertexDataManager(renderer),
      mIndexDataManager(renderer, RENDERER_D3D11)
{
    mCurBlendState.blend                 = false;
    mCurBlendState.sourceBlendRGB        = GL_ONE;
    mCurBlendState.destBlendRGB          = GL_ZERO;
    mCurBlendState.sourceBlendAlpha      = GL_ONE;
    mCurBlendState.destBlendAlpha        = GL_ZERO;
    mCurBlendState.blendEquationRGB      = GL_FUNC_ADD;
    mCurBlendState.blendEquationAlpha    = GL_FUNC_ADD;
    mCurBlendState.colorMaskRed          = true;
    mCurBlendState.colorMaskBlue         = true;
    mCurBlendState.colorMaskGreen        = true;
    mCurBlendState.colorMaskAlpha        = true;
    mCurBlendState.sampleAlphaToCoverage = false;
    mCurBlendState.dither                = false;

    mCurDepthStencilState.depthTest                = false;
    mCurDepthStencilState.depthFunc                = GL_LESS;
    mCurDepthStencilState.depthMask                = true;
    mCurDepthStencilState.stencilTest              = false;
    mCurDepthStencilState.stencilMask              = true;
    mCurDepthStencilState.stencilFail              = GL_KEEP;
    mCurDepthStencilState.stencilPassDepthFail     = GL_KEEP;
    mCurDepthStencilState.stencilPassDepthPass     = GL_KEEP;
    mCurDepthStencilState.stencilWritemask         = static_cast<GLuint>(-1);
    mCurDepthStencilState.stencilBackFunc          = GL_ALWAYS;
    mCurDepthStencilState.stencilBackMask          = static_cast<GLuint>(-1);
    mCurDepthStencilState.stencilBackFail          = GL_KEEP;
    mCurDepthStencilState.stencilBackPassDepthFail = GL_KEEP;
    mCurDepthStencilState.stencilBackPassDepthPass = GL_KEEP;
    mCurDepthStencilState.stencilBackWritemask     = static_cast<GLuint>(-1);

    mCurRasterState.rasterizerDiscard   = false;
    mCurRasterState.cullFace            = false;
    mCurRasterState.cullMode            = GL_BACK;
    mCurRasterState.frontFace           = GL_CCW;
    mCurRasterState.polygonOffsetFill   = false;
    mCurRasterState.polygonOffsetFactor = 0.0f;
    mCurRasterState.polygonOffsetUnits  = 0.0f;
    mCurRasterState.pointDrawMode       = false;
    mCurRasterState.multiSample         = false;

    // Initially all current value attributes must be updated on first use.
    mDirtyCurrentValueAttribs.flip();

    mCurrentVertexBuffers.fill(nullptr);
    mCurrentVertexStrides.fill(std::numeric_limits<UINT>::max());
    mCurrentVertexOffsets.fill(std::numeric_limits<UINT>::max());
}

StateManager11::~StateManager11()
{
}

void StateManager11::updateStencilSizeIfChanged(bool depthStencilInitialized,
                                                unsigned int stencilSize)
{
    if (!depthStencilInitialized || stencilSize != mCurStencilSize)
    {
        mCurStencilSize = stencilSize;
        mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
    }
}

void StateManager11::checkPresentPath(const gl::Context *context)
{
    if (!mRenderer->presentPathFastEnabled())
        return;

    const auto *framebuffer          = context->getGLState().getDrawFramebuffer();
    const auto *firstColorAttachment = framebuffer->getFirstColorbuffer();
    const bool presentPathFastActive = UsePresentPathFast(mRenderer, firstColorAttachment);

    const int colorBufferHeight = firstColorAttachment ? firstColorAttachment->getSize().height : 0;

    if ((mCurPresentPathFastEnabled != presentPathFastActive) ||
        (presentPathFastActive && (colorBufferHeight != mCurPresentPathFastColorBufferHeight)))
    {
        mCurPresentPathFastEnabled           = presentPathFastActive;
        mCurPresentPathFastColorBufferHeight = colorBufferHeight;

        // Scissor rect may need to be vertically inverted
        mInternalDirtyBits.set(DIRTY_BIT_SCISSOR_STATE);

        // Cull Mode may need to be inverted
        mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);

        // Viewport may need to be vertically inverted
        mInternalDirtyBits.set(DIRTY_BIT_VIEWPORT_STATE);
    }
}

gl::Error StateManager11::updateStateForCompute(const gl::Context *context,
                                                GLuint numGroupsX,
                                                GLuint numGroupsY,
                                                GLuint numGroupsZ)
{
    mComputeConstants.numWorkGroups[0] = numGroupsX;
    mComputeConstants.numWorkGroups[1] = numGroupsY;
    mComputeConstants.numWorkGroups[2] = numGroupsZ;

    // TODO(jmadill): More complete implementation.
    ANGLE_TRY(syncTextures(context));

    return gl::NoError();
}

void StateManager11::syncState(const gl::Context *context, const gl::State::DirtyBits &dirtyBits)
{
    if (!dirtyBits.any())
    {
        return;
    }

    const auto &state = context->getGLState();

    for (auto dirtyBit : dirtyBits)
    {
        switch (dirtyBit)
        {
            case gl::State::DIRTY_BIT_BLEND_EQUATIONS:
            {
                const gl::BlendState &blendState = state.getBlendState();
                if (blendState.blendEquationRGB != mCurBlendState.blendEquationRGB ||
                    blendState.blendEquationAlpha != mCurBlendState.blendEquationAlpha)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_BLEND_FUNCS:
            {
                const gl::BlendState &blendState = state.getBlendState();
                if (blendState.sourceBlendRGB != mCurBlendState.sourceBlendRGB ||
                    blendState.destBlendRGB != mCurBlendState.destBlendRGB ||
                    blendState.sourceBlendAlpha != mCurBlendState.sourceBlendAlpha ||
                    blendState.destBlendAlpha != mCurBlendState.destBlendAlpha)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_BLEND_ENABLED:
                if (state.getBlendState().blend != mCurBlendState.blend)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_SAMPLE_ALPHA_TO_COVERAGE_ENABLED:
                if (state.getBlendState().sampleAlphaToCoverage !=
                    mCurBlendState.sampleAlphaToCoverage)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DITHER_ENABLED:
                if (state.getBlendState().dither != mCurBlendState.dither)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_COLOR_MASK:
            {
                const gl::BlendState &blendState = state.getBlendState();
                if (blendState.colorMaskRed != mCurBlendState.colorMaskRed ||
                    blendState.colorMaskGreen != mCurBlendState.colorMaskGreen ||
                    blendState.colorMaskBlue != mCurBlendState.colorMaskBlue ||
                    blendState.colorMaskAlpha != mCurBlendState.colorMaskAlpha)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_BLEND_COLOR:
                if (state.getBlendColor() != mCurBlendColor)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DEPTH_MASK:
                if (state.getDepthStencilState().depthMask != mCurDepthStencilState.depthMask)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DEPTH_TEST_ENABLED:
                if (state.getDepthStencilState().depthTest != mCurDepthStencilState.depthTest)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DEPTH_FUNC:
                if (state.getDepthStencilState().depthFunc != mCurDepthStencilState.depthFunc)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_STENCIL_TEST_ENABLED:
                if (state.getDepthStencilState().stencilTest != mCurDepthStencilState.stencilTest)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_STENCIL_FUNCS_FRONT:
            {
                const gl::DepthStencilState &depthStencil = state.getDepthStencilState();
                if (depthStencil.stencilFunc != mCurDepthStencilState.stencilFunc ||
                    depthStencil.stencilMask != mCurDepthStencilState.stencilMask ||
                    state.getStencilRef() != mCurStencilRef)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_STENCIL_FUNCS_BACK:
            {
                const gl::DepthStencilState &depthStencil = state.getDepthStencilState();
                if (depthStencil.stencilBackFunc != mCurDepthStencilState.stencilBackFunc ||
                    depthStencil.stencilBackMask != mCurDepthStencilState.stencilBackMask ||
                    state.getStencilBackRef() != mCurStencilBackRef)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_STENCIL_WRITEMASK_FRONT:
                if (state.getDepthStencilState().stencilWritemask !=
                    mCurDepthStencilState.stencilWritemask)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_STENCIL_WRITEMASK_BACK:
                if (state.getDepthStencilState().stencilBackWritemask !=
                    mCurDepthStencilState.stencilBackWritemask)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_STENCIL_OPS_FRONT:
            {
                const gl::DepthStencilState &depthStencil = state.getDepthStencilState();
                if (depthStencil.stencilFail != mCurDepthStencilState.stencilFail ||
                    depthStencil.stencilPassDepthFail !=
                        mCurDepthStencilState.stencilPassDepthFail ||
                    depthStencil.stencilPassDepthPass != mCurDepthStencilState.stencilPassDepthPass)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_STENCIL_OPS_BACK:
            {
                const gl::DepthStencilState &depthStencil = state.getDepthStencilState();
                if (depthStencil.stencilBackFail != mCurDepthStencilState.stencilBackFail ||
                    depthStencil.stencilBackPassDepthFail !=
                        mCurDepthStencilState.stencilBackPassDepthFail ||
                    depthStencil.stencilBackPassDepthPass !=
                        mCurDepthStencilState.stencilBackPassDepthPass)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_CULL_FACE_ENABLED:
                if (state.getRasterizerState().cullFace != mCurRasterState.cullFace)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_CULL_FACE:
                if (state.getRasterizerState().cullMode != mCurRasterState.cullMode)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_FRONT_FACE:
                if (state.getRasterizerState().frontFace != mCurRasterState.frontFace)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_POLYGON_OFFSET_FILL_ENABLED:
                if (state.getRasterizerState().polygonOffsetFill !=
                    mCurRasterState.polygonOffsetFill)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_POLYGON_OFFSET:
            {
                const gl::RasterizerState &rasterState = state.getRasterizerState();
                if (rasterState.polygonOffsetFactor != mCurRasterState.polygonOffsetFactor ||
                    rasterState.polygonOffsetUnits != mCurRasterState.polygonOffsetUnits)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            }
            case gl::State::DIRTY_BIT_RASTERIZER_DISCARD_ENABLED:
                if (state.getRasterizerState().rasterizerDiscard !=
                    mCurRasterState.rasterizerDiscard)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_SCISSOR:
                if (state.getScissor() != mCurScissorRect)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_SCISSOR_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_SCISSOR_TEST_ENABLED:
                if (state.isScissorTestEnabled() != mCurScissorEnabled)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_SCISSOR_STATE);
                    // Rasterizer state update needs mCurScissorsEnabled and updates when it changes
                    mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DEPTH_RANGE:
                if (state.getNearPlane() != mCurNear || state.getFarPlane() != mCurFar)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_VIEWPORT_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_VIEWPORT:
                if (state.getViewport() != mCurViewport)
                {
                    mInternalDirtyBits.set(DIRTY_BIT_VIEWPORT_STATE);
                }
                break;
            case gl::State::DIRTY_BIT_DRAW_FRAMEBUFFER_BINDING:
                invalidateRenderTarget(context);
                break;
            case gl::State::DIRTY_BIT_VERTEX_ARRAY_BINDING:
                invalidateVertexBuffer();
                break;
            case gl::State::DIRTY_BIT_PROGRAM_EXECUTABLE:
                invalidateVertexBuffer();
                invalidateRenderTarget(context);
                break;
            default:
                if (dirtyBit >= gl::State::DIRTY_BIT_CURRENT_VALUE_0 &&
                    dirtyBit < gl::State::DIRTY_BIT_CURRENT_VALUE_MAX)
                {
                    size_t attribIndex =
                        static_cast<size_t>(dirtyBit - gl::State::DIRTY_BIT_CURRENT_VALUE_0);
                    mDirtyCurrentValueAttribs.set(attribIndex);
                }
                break;
        }
    }

    // TODO(jmadill): Input layout and vertex buffer state.
}

gl::Error StateManager11::syncBlendState(const gl::Context *context,
                                         const gl::Framebuffer *framebuffer,
                                         const gl::BlendState &blendState,
                                         const gl::ColorF &blendColor,
                                         unsigned int sampleMask)
{
    ID3D11BlendState *dxBlendState = nullptr;
    const d3d11::BlendStateKey &key =
        RenderStateCache::GetBlendStateKey(context, framebuffer, blendState);

    ANGLE_TRY(mRenderer->getBlendState(key, &dxBlendState));

    ASSERT(dxBlendState != nullptr);

    float blendColors[4] = {0.0f};
    if (blendState.sourceBlendRGB != GL_CONSTANT_ALPHA &&
        blendState.sourceBlendRGB != GL_ONE_MINUS_CONSTANT_ALPHA &&
        blendState.destBlendRGB != GL_CONSTANT_ALPHA &&
        blendState.destBlendRGB != GL_ONE_MINUS_CONSTANT_ALPHA)
    {
        blendColors[0] = blendColor.red;
        blendColors[1] = blendColor.green;
        blendColors[2] = blendColor.blue;
        blendColors[3] = blendColor.alpha;
    }
    else
    {
        blendColors[0] = blendColor.alpha;
        blendColors[1] = blendColor.alpha;
        blendColors[2] = blendColor.alpha;
        blendColors[3] = blendColor.alpha;
    }

    mRenderer->getDeviceContext()->OMSetBlendState(dxBlendState, blendColors, sampleMask);

    mCurBlendState = blendState;
    mCurBlendColor = blendColor;
    mCurSampleMask = sampleMask;

    return gl::NoError();
}

gl::Error StateManager11::syncDepthStencilState(const gl::State &glState)
{
    mCurDepthStencilState = glState.getDepthStencilState();
    mCurStencilRef        = glState.getStencilRef();
    mCurStencilBackRef    = glState.getStencilBackRef();

    // get the maximum size of the stencil ref
    unsigned int maxStencil = 0;
    if (mCurDepthStencilState.stencilTest && mCurStencilSize > 0)
    {
        maxStencil = (1 << mCurStencilSize) - 1;
    }
    ASSERT((mCurDepthStencilState.stencilWritemask & maxStencil) ==
           (mCurDepthStencilState.stencilBackWritemask & maxStencil));
    ASSERT(mCurStencilRef == mCurStencilBackRef);
    ASSERT((mCurDepthStencilState.stencilMask & maxStencil) ==
           (mCurDepthStencilState.stencilBackMask & maxStencil));

    gl::DepthStencilState modifiedGLState = glState.getDepthStencilState();

    ASSERT(mCurDisableDepth.valid() && mCurDisableStencil.valid());

    if (mCurDisableDepth.value())
    {
        modifiedGLState.depthTest = false;
        modifiedGLState.depthMask = false;
    }

    if (mCurDisableStencil.value())
    {
        modifiedGLState.stencilWritemask     = 0;
        modifiedGLState.stencilBackWritemask = 0;
        modifiedGLState.stencilTest          = false;
    }

    ID3D11DepthStencilState *d3dState = nullptr;
    ANGLE_TRY(mRenderer->getDepthStencilState(modifiedGLState, &d3dState));
    ASSERT(d3dState);

    // Max D3D11 stencil reference value is 0xFF,
    // corresponding to the max 8 bits in a stencil buffer
    // GL specifies we should clamp the ref value to the
    // nearest bit depth when doing stencil ops
    static_assert(D3D11_DEFAULT_STENCIL_READ_MASK == 0xFF,
                  "Unexpected value of D3D11_DEFAULT_STENCIL_READ_MASK");
    static_assert(D3D11_DEFAULT_STENCIL_WRITE_MASK == 0xFF,
                  "Unexpected value of D3D11_DEFAULT_STENCIL_WRITE_MASK");
    UINT dxStencilRef = std::min<UINT>(mCurStencilRef, 0xFFu);

    mRenderer->getDeviceContext()->OMSetDepthStencilState(d3dState, dxStencilRef);

    return gl::NoError();
}

gl::Error StateManager11::syncRasterizerState(const gl::Context *context, bool pointDrawMode)
{
    // TODO: Remove pointDrawMode and multiSample from gl::RasterizerState.
    gl::RasterizerState rasterState = context->getGLState().getRasterizerState();
    rasterState.pointDrawMode       = pointDrawMode;
    rasterState.multiSample         = mCurRasterState.multiSample;

    ID3D11RasterizerState *dxRasterState = nullptr;

    if (mCurPresentPathFastEnabled)
    {
        gl::RasterizerState modifiedRasterState = rasterState;

        // If prseent path fast is active then we need invert the front face state.
        // This ensures that both gl_FrontFacing is correct, and front/back culling
        // is performed correctly.
        if (modifiedRasterState.frontFace == GL_CCW)
        {
            modifiedRasterState.frontFace = GL_CW;
        }
        else
        {
            ASSERT(modifiedRasterState.frontFace == GL_CW);
            modifiedRasterState.frontFace = GL_CCW;
        }

        ANGLE_TRY(
            mRenderer->getRasterizerState(modifiedRasterState, mCurScissorEnabled, &dxRasterState));
    }
    else
    {
        ANGLE_TRY(mRenderer->getRasterizerState(rasterState, mCurScissorEnabled, &dxRasterState));
    }

    mRenderer->getDeviceContext()->RSSetState(dxRasterState);

    mCurRasterState = rasterState;

    return gl::NoError();
}

void StateManager11::syncScissorRectangle(const gl::Rectangle &scissor, bool enabled)
{
    int modifiedScissorY = scissor.y;
    if (mCurPresentPathFastEnabled)
    {
        modifiedScissorY = mCurPresentPathFastColorBufferHeight - scissor.height - scissor.y;
    }

    if (enabled)
    {
        D3D11_RECT rect;
        rect.left   = std::max(0, scissor.x);
        rect.top    = std::max(0, modifiedScissorY);
        rect.right  = scissor.x + std::max(0, scissor.width);
        rect.bottom = modifiedScissorY + std::max(0, scissor.height);

        mRenderer->getDeviceContext()->RSSetScissorRects(1, &rect);
    }

    mCurScissorRect      = scissor;
    mCurScissorEnabled   = enabled;
}

void StateManager11::syncViewport(const gl::Context *context)
{
    const auto &glState          = context->getGLState();
    gl::Framebuffer *framebuffer = glState.getDrawFramebuffer();
    float actualZNear            = gl::clamp01(glState.getNearPlane());
    float actualZFar             = gl::clamp01(glState.getFarPlane());

    const auto &caps         = context->getCaps();
    int dxMaxViewportBoundsX = static_cast<int>(caps.maxViewportWidth);
    int dxMaxViewportBoundsY = static_cast<int>(caps.maxViewportHeight);
    int dxMinViewportBoundsX = -dxMaxViewportBoundsX;
    int dxMinViewportBoundsY = -dxMaxViewportBoundsY;

    if (mRenderer->getRenderer11DeviceCaps().featureLevel <= D3D_FEATURE_LEVEL_9_3)
    {
        // Feature Level 9 viewports shouldn't exceed the dimensions of the rendertarget.
        dxMaxViewportBoundsX = static_cast<int>(mViewportBounds.width);
        dxMaxViewportBoundsY = static_cast<int>(mViewportBounds.height);
        dxMinViewportBoundsX = 0;
        dxMinViewportBoundsY = 0;
    }

    const auto &viewport   = glState.getViewport();
    int dxViewportTopLeftX = gl::clamp(viewport.x, dxMinViewportBoundsX, dxMaxViewportBoundsX);
    int dxViewportTopLeftY = gl::clamp(viewport.y, dxMinViewportBoundsY, dxMaxViewportBoundsY);
    int dxViewportWidth    = gl::clamp(viewport.width, 0, dxMaxViewportBoundsX - dxViewportTopLeftX);
    int dxViewportHeight   = gl::clamp(viewport.height, 0, dxMaxViewportBoundsY - dxViewportTopLeftY);

    D3D11_VIEWPORT dxViewport;
    dxViewport.TopLeftX = static_cast<float>(dxViewportTopLeftX);

    if (mCurPresentPathFastEnabled)
    {
        // When present path fast is active and we're rendering to framebuffer 0, we must invert
        // the viewport in Y-axis.
        // NOTE: We delay the inversion until right before the call to RSSetViewports, and leave
        // dxViewportTopLeftY unchanged. This allows us to calculate viewAdjust below using the
        // unaltered dxViewportTopLeftY value.
        dxViewport.TopLeftY = static_cast<float>(mCurPresentPathFastColorBufferHeight -
                                                 dxViewportTopLeftY - dxViewportHeight);
    }
    else
    {
        dxViewport.TopLeftY = static_cast<float>(dxViewportTopLeftY);
    }

    dxViewport.Width    = static_cast<float>(dxViewportWidth);
    dxViewport.Height   = static_cast<float>(dxViewportHeight);
    dxViewport.MinDepth = actualZNear;
    dxViewport.MaxDepth = actualZFar;

    // The es 3.1 spec section 9.2 states that, "If there are no attachments, rendering
    // will be limited to a rectangle having a lower left of (0, 0) and an upper right of
    // (width, height), where width and height are the framebuffer object's default width
    // and height." See http://anglebug.com/1594
    // If the Framebuffer has no color attachment and the default width or height is smaller
    // than the current viewport, use the smaller of the two sizes.
    // If framebuffer default width or height is 0, the params should not set.
    if (!framebuffer->getFirstNonNullAttachment() &&
        (framebuffer->getDefaultWidth() || framebuffer->getDefaultHeight()))
    {
        dxViewport.Width =
            static_cast<GLfloat>(std::min(viewport.width, framebuffer->getDefaultWidth()));
        dxViewport.Height =
            static_cast<GLfloat>(std::min(viewport.height, framebuffer->getDefaultHeight()));
    }

    mRenderer->getDeviceContext()->RSSetViewports(1, &dxViewport);

    mCurViewport = viewport;
    mCurNear     = actualZNear;
    mCurFar      = actualZFar;

    // On Feature Level 9_*, we must emulate large and/or negative viewports in the shaders
    // using viewAdjust (like the D3D9 renderer).
    if (mRenderer->getRenderer11DeviceCaps().featureLevel <= D3D_FEATURE_LEVEL_9_3)
    {
        mVertexConstants.viewAdjust[0] = static_cast<float>((viewport.width - dxViewportWidth) +
                                                            2 * (viewport.x - dxViewportTopLeftX)) /
                                         dxViewport.Width;
        mVertexConstants.viewAdjust[1] = static_cast<float>((viewport.height - dxViewportHeight) +
                                                            2 * (viewport.y - dxViewportTopLeftY)) /
                                         dxViewport.Height;
        mVertexConstants.viewAdjust[2] = static_cast<float>(viewport.width) / dxViewport.Width;
        mVertexConstants.viewAdjust[3] = static_cast<float>(viewport.height) / dxViewport.Height;
    }

    mPixelConstants.viewCoords[0] = viewport.width * 0.5f;
    mPixelConstants.viewCoords[1] = viewport.height * 0.5f;
    mPixelConstants.viewCoords[2] = viewport.x + (viewport.width * 0.5f);
    mPixelConstants.viewCoords[3] = viewport.y + (viewport.height * 0.5f);

    // Instanced pointsprite emulation requires ViewCoords to be defined in the
    // the vertex shader.
    mVertexConstants.viewCoords[0] = mPixelConstants.viewCoords[0];
    mVertexConstants.viewCoords[1] = mPixelConstants.viewCoords[1];
    mVertexConstants.viewCoords[2] = mPixelConstants.viewCoords[2];
    mVertexConstants.viewCoords[3] = mPixelConstants.viewCoords[3];

    mPixelConstants.depthFront[0] = (actualZFar - actualZNear) * 0.5f;
    mPixelConstants.depthFront[1] = (actualZNear + actualZFar) * 0.5f;

    mVertexConstants.depthRange[0] = actualZNear;
    mVertexConstants.depthRange[1] = actualZFar;
    mVertexConstants.depthRange[2] = actualZFar - actualZNear;

    mPixelConstants.depthRange[0] = actualZNear;
    mPixelConstants.depthRange[1] = actualZFar;
    mPixelConstants.depthRange[2] = actualZFar - actualZNear;

    mPixelConstants.viewScale[0] = 1.0f;
    mPixelConstants.viewScale[1] = mCurPresentPathFastEnabled ? 1.0f : -1.0f;
    mPixelConstants.viewScale[2] = 1.0f;
    mPixelConstants.viewScale[3] = 1.0f;

    mVertexConstants.viewScale[0] = mPixelConstants.viewScale[0];
    mVertexConstants.viewScale[1] = mPixelConstants.viewScale[1];
    mVertexConstants.viewScale[2] = mPixelConstants.viewScale[2];
    mVertexConstants.viewScale[3] = mPixelConstants.viewScale[3];
}

void StateManager11::invalidateRenderTarget(const gl::Context *context)
{
    mInternalDirtyBits.set(DIRTY_BIT_RENDER_TARGET);

    // The D3D11 blend state is heavily dependent on the current render target.
    mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);

    // nullptr only on display initialization.
    if (!context)
    {
        return;
    }

    gl::Framebuffer *fbo = context->getGLState().getDrawFramebuffer();

    // nullptr fbo can occur in some egl events like display initialization.
    if (!fbo)
    {
        return;
    }

    // Disable the depth test/depth write if we are using a stencil-only attachment.
    // This is because ANGLE emulates stencil-only with D24S8 on D3D11 - we should neither read
    // nor write to the unused depth part of this emulated texture.
    bool disableDepth = (!fbo->hasDepth() && fbo->hasStencil());

    // Similarly we disable the stencil portion of the DS attachment if the app only binds depth.
    bool disableStencil = (fbo->hasDepth() && !fbo->hasStencil());

    if (!mCurDisableDepth.valid() || disableDepth != mCurDisableDepth.value() ||
        !mCurDisableStencil.valid() || disableStencil != mCurDisableStencil.value())
    {
        mInternalDirtyBits.set(DIRTY_BIT_DEPTH_STENCIL_STATE);
        mCurDisableDepth   = disableDepth;
        mCurDisableStencil = disableStencil;
    }

    bool multiSample = (fbo->getCachedSamples(context) != 0);
    if (multiSample != mCurRasterState.multiSample)
    {
        mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
        mCurRasterState.multiSample = multiSample;
    }

    checkPresentPath(context);

    if (mRenderer->getRenderer11DeviceCaps().featureLevel <= D3D_FEATURE_LEVEL_9_3)
    {
        auto *firstAttachment = fbo->getFirstNonNullAttachment();
        const auto &size      = firstAttachment->getSize();
        if (mViewportBounds.width != size.width || mViewportBounds.height != size.height)
        {
            mViewportBounds = gl::Extents(size.width, size.height, 1);
            mInternalDirtyBits.set(DIRTY_BIT_VIEWPORT_STATE);
        }
    }
}

void StateManager11::invalidateBoundViews(const gl::Context *context)
{
    mCurVertexSRVs.clear();
    mCurPixelSRVs.clear();

    invalidateRenderTarget(context);
}

void StateManager11::invalidateEverything(const gl::Context *context)
{
    mInternalDirtyBits.set();

    // We reset the current SRV data because it might not be in sync with D3D's state
    // anymore. For example when a currently used SRV is used as an RTV, D3D silently
    // remove it from its state.
    invalidateBoundViews(context);

    // All calls to IASetInputLayout go through the state manager, so it shouldn't be
    // necessary to invalidate the state.

    // Invalidate the vertex buffer state.
    invalidateVertexBuffer();

    mCurrentPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    mAppliedVertexShader.dirty();
    mAppliedGeometryShader.dirty();
    mAppliedPixelShader.dirty();
    mAppliedComputeShader.dirty();

    std::fill(mForceSetVertexSamplerStates.begin(), mForceSetVertexSamplerStates.end(), true);
    std::fill(mForceSetPixelSamplerStates.begin(), mForceSetPixelSamplerStates.end(), true);
    std::fill(mForceSetComputeSamplerStates.begin(), mForceSetComputeSamplerStates.end(), true);

    mAppliedIB       = nullptr;
    mAppliedIBFormat = DXGI_FORMAT_UNKNOWN;
    mAppliedIBOffset = 0;

    mLastFirstVertex.reset();
}

void StateManager11::invalidateVertexBuffer()
{
    unsigned int limit = std::min<unsigned int>(mRenderer->getNativeCaps().maxVertexAttributes,
                                                gl::MAX_VERTEX_ATTRIBS);
    mDirtyVertexBufferRange = gl::RangeUI(0, limit);
    mInputLayoutIsDirty     = true;
}

void StateManager11::invalidateViewport(const gl::Context *context)
{
    mInternalDirtyBits.set(DIRTY_BIT_VIEWPORT_STATE);
}

void StateManager11::setOneTimeRenderTarget(const gl::Context *context,
                                            ID3D11RenderTargetView *rtv,
                                            ID3D11DepthStencilView *dsv)
{
    mRenderer->getDeviceContext()->OMSetRenderTargets(1, &rtv, dsv);
    invalidateRenderTarget(context);
}

void StateManager11::setOneTimeRenderTargets(const gl::Context *context,
                                             ID3D11RenderTargetView **rtvs,
                                             UINT numRtvs,
                                             ID3D11DepthStencilView *dsv)
{
    mRenderer->getDeviceContext()->OMSetRenderTargets(numRtvs, (numRtvs > 0) ? rtvs : nullptr, dsv);
    invalidateRenderTarget(context);
}

void StateManager11::onBeginQuery(Query11 *query)
{
    mCurrentQueries.insert(query);
}

void StateManager11::onDeleteQueryObject(Query11 *query)
{
    mCurrentQueries.erase(query);
}

gl::Error StateManager11::onMakeCurrent(const gl::Context *context)
{
    const gl::State &state = context->getGLState();

    for (Query11 *query : mCurrentQueries)
    {
        query->pause();
    }
    mCurrentQueries.clear();

    for (GLenum queryType : QueryTypes)
    {
        gl::Query *query = state.getActiveQuery(queryType);
        if (query != nullptr)
        {
            Query11 *query11 = GetImplAs<Query11>(query);
            query11->resume();
            mCurrentQueries.insert(query11);
        }
    }

    return gl::NoError();
}

void StateManager11::setShaderResource(gl::SamplerType shaderType,
                                       UINT resourceSlot,
                                       ID3D11ShaderResourceView *srv)
{
    auto &currentSRVs = (shaderType == gl::SAMPLER_VERTEX ? mCurVertexSRVs : mCurPixelSRVs);

    ASSERT(static_cast<size_t>(resourceSlot) < currentSRVs.size());
    const SRVRecord &record = currentSRVs[resourceSlot];

    if (record.srv != reinterpret_cast<uintptr_t>(srv))
    {
        auto deviceContext = mRenderer->getDeviceContext();
        if (shaderType == gl::SAMPLER_VERTEX)
        {
            deviceContext->VSSetShaderResources(resourceSlot, 1, &srv);
        }
        else
        {
            deviceContext->PSSetShaderResources(resourceSlot, 1, &srv);
        }

        currentSRVs.update(resourceSlot, srv);
    }
}

gl::Error StateManager11::clearTextures(gl::SamplerType samplerType,
                                        size_t rangeStart,
                                        size_t rangeEnd)
{
    if (rangeStart == rangeEnd)
    {
        return gl::NoError();
    }

    auto &currentSRVs = (samplerType == gl::SAMPLER_VERTEX ? mCurVertexSRVs : mCurPixelSRVs);

    gl::Range<size_t> clearRange(rangeStart, std::min(rangeEnd, currentSRVs.highestUsed()));
    if (clearRange.empty())
    {
        return gl::NoError();
    }

    auto deviceContext = mRenderer->getDeviceContext();
    if (samplerType == gl::SAMPLER_VERTEX)
    {
        deviceContext->VSSetShaderResources(static_cast<unsigned int>(clearRange.low()),
                                            static_cast<unsigned int>(clearRange.length()),
                                            &mNullSRVs[0]);
    }
    else
    {
        deviceContext->PSSetShaderResources(static_cast<unsigned int>(clearRange.low()),
                                            static_cast<unsigned int>(clearRange.length()),
                                            &mNullSRVs[0]);
    }

    for (size_t samplerIndex : clearRange)
    {
        currentSRVs.update(samplerIndex, nullptr);
    }

    return gl::NoError();
}

void StateManager11::unsetConflictingSRVs(gl::SamplerType samplerType,
                                          uintptr_t resource,
                                          const gl::ImageIndex &index)
{
    auto &currentSRVs = (samplerType == gl::SAMPLER_VERTEX ? mCurVertexSRVs : mCurPixelSRVs);

    for (size_t resourceIndex = 0; resourceIndex < currentSRVs.size(); ++resourceIndex)
    {
        auto &record = currentSRVs[resourceIndex];

        if (record.srv && record.resource == resource &&
            ImageIndexConflictsWithSRV(index, record.desc))
        {
            setShaderResource(samplerType, static_cast<UINT>(resourceIndex), nullptr);
        }
    }
}

void StateManager11::unsetConflictingAttachmentResources(
    const gl::FramebufferAttachment *attachment,
    ID3D11Resource *resource)
{
    // Unbind render target SRVs from the shader here to prevent D3D11 warnings.
    if (attachment->type() == GL_TEXTURE)
    {
        uintptr_t resourcePtr       = reinterpret_cast<uintptr_t>(resource);
        const gl::ImageIndex &index = attachment->getTextureImageIndex();
        // The index doesn't need to be corrected for the small compressed texture workaround
        // because a rendertarget is never compressed.
        unsetConflictingSRVs(gl::SAMPLER_VERTEX, resourcePtr, index);
        unsetConflictingSRVs(gl::SAMPLER_PIXEL, resourcePtr, index);
    }
}

gl::Error StateManager11::initialize(const gl::Caps &caps)
{
    mCurVertexSRVs.initialize(caps.maxVertexTextureImageUnits);
    mCurPixelSRVs.initialize(caps.maxTextureImageUnits);

    // Initialize cached NULL SRV block
    mNullSRVs.resize(caps.maxTextureImageUnits, nullptr);

    mCurrentValueAttribs.resize(caps.maxVertexAttributes);

    mForceSetVertexSamplerStates.resize(caps.maxVertexTextureImageUnits);
    mForceSetPixelSamplerStates.resize(caps.maxTextureImageUnits);
    mForceSetComputeSamplerStates.resize(caps.maxComputeTextureImageUnits);

    mCurVertexSamplerStates.resize(caps.maxVertexTextureImageUnits);
    mCurPixelSamplerStates.resize(caps.maxTextureImageUnits);
    mCurComputeSamplerStates.resize(caps.maxComputeTextureImageUnits);

    mSamplerMetadataVS.initData(caps.maxVertexTextureImageUnits);
    mSamplerMetadataPS.initData(caps.maxTextureImageUnits);
    mSamplerMetadataCS.initData(caps.maxComputeTextureImageUnits);

    ANGLE_TRY(mVertexDataManager.initialize());

    mCurrentAttributes.reserve(gl::MAX_VERTEX_ATTRIBS);

    return gl::NoError();
}

void StateManager11::deinitialize()
{
    mCurrentValueAttribs.clear();
    mInputLayoutCache.clear();
    mVertexDataManager.deinitialize();
    mIndexDataManager.deinitialize();
}

gl::Error StateManager11::syncFramebuffer(const gl::Context *context, gl::Framebuffer *framebuffer)
{
    Framebuffer11 *framebuffer11 = GetImplAs<Framebuffer11>(framebuffer);

    // Applies the render target surface, depth stencil surface, viewport rectangle and
    // scissor rectangle to the renderer
    ASSERT(framebuffer && !framebuffer->hasAnyDirtyBit() && framebuffer->cachedComplete());

    // Check for zero-sized default framebuffer, which is a special case.
    // in this case we do not wish to modify any state and just silently return false.
    // this will not report any gl error but will cause the calling method to return.
    if (framebuffer->id() == 0)
    {
        ASSERT(!framebuffer11->hasAnyInternalDirtyBit());
        const gl::Extents &size = framebuffer->getFirstColorbuffer()->getSize();
        if (size.width == 0 || size.height == 0)
        {
            return gl::NoError();
        }
    }

    RTVArray framebufferRTVs = {{}};

    const auto &colorRTs = framebuffer11->getCachedColorRenderTargets();

    size_t appliedRTIndex  = 0;
    bool skipInactiveRTs   = mRenderer->getWorkarounds().mrtPerfWorkaround;
    const auto &drawStates = framebuffer->getDrawBufferStates();
    gl::DrawBufferMask activeProgramOutputs =
        context->getContextState().getState().getProgram()->getActiveOutputVariables();
    UINT maxExistingRT = 0;

    for (size_t rtIndex = 0; rtIndex < colorRTs.size(); ++rtIndex)
    {
        const RenderTarget11 *renderTarget = colorRTs[rtIndex];

        // Skip inactive rendertargets if the workaround is enabled.
        if (skipInactiveRTs &&
            (!renderTarget || drawStates[rtIndex] == GL_NONE || !activeProgramOutputs[rtIndex]))
        {
            continue;
        }

        if (renderTarget)
        {
            framebufferRTVs[appliedRTIndex] = renderTarget->getRenderTargetView().get();
            ASSERT(framebufferRTVs[appliedRTIndex]);
            maxExistingRT = static_cast<UINT>(appliedRTIndex) + 1;
        }

        // Unset conflicting texture SRVs
        const auto *attachment = framebuffer->getColorbuffer(rtIndex);
        ASSERT(attachment);
        unsetConflictingAttachmentResources(attachment, renderTarget->getTexture().get());

        appliedRTIndex++;
    }

    // Get the depth stencil buffers
    ID3D11DepthStencilView *framebufferDSV = nullptr;
    const auto *depthStencilRenderTarget   = framebuffer11->getCachedDepthStencilRenderTarget();
    if (depthStencilRenderTarget)
    {
        framebufferDSV = depthStencilRenderTarget->getDepthStencilView().get();
        ASSERT(framebufferDSV);

        // Unset conflicting texture SRVs
        const auto *attachment = framebuffer->getDepthOrStencilbuffer();
        ASSERT(attachment);
        unsetConflictingAttachmentResources(attachment,
                                            depthStencilRenderTarget->getTexture().get());
    }

    // TODO(jmadill): Use context caps?
    ASSERT(maxExistingRT <= static_cast<UINT>(mRenderer->getNativeCaps().maxDrawBuffers));

    // Apply the render target and depth stencil
    mRenderer->getDeviceContext()->OMSetRenderTargets(maxExistingRT, framebufferRTVs.data(),
                                                      framebufferDSV);

    return gl::NoError();
}

void StateManager11::invalidateCurrentValueAttrib(size_t attribIndex)
{
    mDirtyCurrentValueAttribs.set(attribIndex);
}

gl::Error StateManager11::syncCurrentValueAttribs(const gl::State &state)
{
    const auto &activeAttribsMask  = state.getProgram()->getActiveAttribLocationsMask();
    const auto &dirtyActiveAttribs = (activeAttribsMask & mDirtyCurrentValueAttribs);
    const auto &vertexAttributes   = state.getVertexArray()->getVertexAttributes();
    const auto &vertexBindings     = state.getVertexArray()->getVertexBindings();

    if (!dirtyActiveAttribs.any())
    {
        return gl::NoError();
    }

    invalidateVertexBuffer();
    mDirtyCurrentValueAttribs = (mDirtyCurrentValueAttribs & ~dirtyActiveAttribs);

    for (auto attribIndex : dirtyActiveAttribs)
    {
        if (vertexAttributes[attribIndex].enabled)
            continue;

        const auto *attrib                   = &vertexAttributes[attribIndex];
        const auto &currentValue             = state.getVertexAttribCurrentValue(attribIndex);
        auto currentValueAttrib              = &mCurrentValueAttribs[attribIndex];
        currentValueAttrib->currentValueType = currentValue.Type;
        currentValueAttrib->attribute        = attrib;
        currentValueAttrib->binding          = &vertexBindings[attrib->bindingIndex];

        ANGLE_TRY(mVertexDataManager.storeCurrentValue(currentValue, currentValueAttrib,
                                                       static_cast<size_t>(attribIndex)));
    }

    return gl::NoError();
}

void StateManager11::setInputLayout(const d3d11::InputLayout *inputLayout)
{
    ID3D11DeviceContext *deviceContext = mRenderer->getDeviceContext();
    if (inputLayout == nullptr)
    {
        if (mCurrentInputLayout != 0)
        {
            deviceContext->IASetInputLayout(nullptr);
            mCurrentInputLayout = 0;
            mInputLayoutIsDirty = true;
        }
    }
    else if (inputLayout->getSerial() != mCurrentInputLayout)
    {
        deviceContext->IASetInputLayout(inputLayout->get());
        mCurrentInputLayout = inputLayout->getSerial();
        mInputLayoutIsDirty = true;
    }
}

bool StateManager11::queueVertexBufferChange(size_t bufferIndex,
                                             ID3D11Buffer *buffer,
                                             UINT stride,
                                             UINT offset)
{
    if (buffer != mCurrentVertexBuffers[bufferIndex] ||
        stride != mCurrentVertexStrides[bufferIndex] ||
        offset != mCurrentVertexOffsets[bufferIndex])
    {
        mInputLayoutIsDirty = true;
        mDirtyVertexBufferRange.extend(static_cast<unsigned int>(bufferIndex));

        mCurrentVertexBuffers[bufferIndex] = buffer;
        mCurrentVertexStrides[bufferIndex] = stride;
        mCurrentVertexOffsets[bufferIndex] = offset;
        return true;
    }

    return false;
}

bool StateManager11::queueVertexOffsetChange(size_t bufferIndex, UINT offsetOnly)
{
    if (offsetOnly != mCurrentVertexOffsets[bufferIndex])
    {
        mInputLayoutIsDirty = true;
        mDirtyVertexBufferRange.extend(static_cast<unsigned int>(bufferIndex));
        mCurrentVertexOffsets[bufferIndex] = offsetOnly;
        return true;
    }
    return false;
}

void StateManager11::applyVertexBufferChanges()
{
    if (mDirtyVertexBufferRange.empty())
    {
        return;
    }

    ASSERT(mDirtyVertexBufferRange.high() <= gl::MAX_VERTEX_ATTRIBS);

    UINT start = static_cast<UINT>(mDirtyVertexBufferRange.low());

    ID3D11DeviceContext *deviceContext = mRenderer->getDeviceContext();
    deviceContext->IASetVertexBuffers(start, static_cast<UINT>(mDirtyVertexBufferRange.length()),
                                      &mCurrentVertexBuffers[start], &mCurrentVertexStrides[start],
                                      &mCurrentVertexOffsets[start]);

    mDirtyVertexBufferRange = gl::RangeUI(gl::MAX_VERTEX_ATTRIBS, 0);
}

void StateManager11::setSingleVertexBuffer(const d3d11::Buffer *buffer, UINT stride, UINT offset)
{
    ID3D11Buffer *native = buffer ? buffer->get() : nullptr;
    if (queueVertexBufferChange(0, native, stride, offset))
    {
        applyVertexBufferChanges();
    }
}

gl::Error StateManager11::updateState(const gl::Context *context, GLenum drawMode)
{
    const auto &glState = context->getGLState();

    // TODO(jmadill): Use dirty bits.
    ANGLE_TRY(syncProgram(context, drawMode));

    gl::Framebuffer *framebuffer = glState.getDrawFramebuffer();
    Framebuffer11 *framebuffer11 = GetImplAs<Framebuffer11>(framebuffer);
    ANGLE_TRY(framebuffer11->markAttachmentsDirty(context));

    if (framebuffer11->hasAnyInternalDirtyBit())
    {
        ASSERT(framebuffer->id() != 0);
        framebuffer11->syncInternalState(context);
    }

    bool pointDrawMode = (drawMode == GL_POINTS);
    if (pointDrawMode != mCurRasterState.pointDrawMode)
    {
        mInternalDirtyBits.set(DIRTY_BIT_RASTERIZER_STATE);
    }

    // TODO(jmadill): This can be recomputed only on framebuffer changes.
    RenderTarget11 *firstRT = framebuffer11->getFirstRenderTarget();
    int samples             = (firstRT ? firstRT->getSamples() : 0);
    unsigned int sampleMask = GetBlendSampleMask(glState, samples);
    if (sampleMask != mCurSampleMask)
    {
        mInternalDirtyBits.set(DIRTY_BIT_BLEND_STATE);
    }

    auto dirtyBitsCopy = mInternalDirtyBits;
    mInternalDirtyBits.reset();

    for (auto dirtyBit : dirtyBitsCopy)
    {
        switch (dirtyBit)
        {
            case DIRTY_BIT_RENDER_TARGET:
                ANGLE_TRY(syncFramebuffer(context, framebuffer));
                break;
            case DIRTY_BIT_VIEWPORT_STATE:
                syncViewport(context);
                break;
            case DIRTY_BIT_SCISSOR_STATE:
                syncScissorRectangle(glState.getScissor(), glState.isScissorTestEnabled());
                break;
            case DIRTY_BIT_RASTERIZER_STATE:
                ANGLE_TRY(syncRasterizerState(context, pointDrawMode));
                break;
            case DIRTY_BIT_BLEND_STATE:
                ANGLE_TRY(syncBlendState(context, framebuffer, glState.getBlendState(),
                                         glState.getBlendColor(), sampleMask));
                break;
            case DIRTY_BIT_DEPTH_STENCIL_STATE:
                ANGLE_TRY(syncDepthStencilState(glState));
                break;
            default:
                UNREACHABLE();
                break;
        }
    }

    // TODO(jmadill): Use dirty bits.
    ANGLE_TRY(syncTextures(context));

    // This must happen after viewport sync, because the viewport affects builtin uniforms.
    // TODO(jmadill): Use dirty bits.
    auto *programD3D = GetImplAs<ProgramD3D>(glState.getProgram());
    ANGLE_TRY(programD3D->applyUniforms(drawMode));

    // Check that we haven't set any dirty bits in the flushing of the dirty bits loop.
    ASSERT(mInternalDirtyBits.none());

    return gl::NoError();
}

void StateManager11::setPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY primitiveTopology)
{
    if (primitiveTopology != mCurrentPrimitiveTopology)
    {
        mRenderer->getDeviceContext()->IASetPrimitiveTopology(primitiveTopology);
        mCurrentPrimitiveTopology = primitiveTopology;
    }
}

void StateManager11::setDrawShaders(const d3d11::VertexShader *vertexShader,
                                    const d3d11::GeometryShader *geometryShader,
                                    const d3d11::PixelShader *pixelShader)
{
    setVertexShader(vertexShader);
    setGeometryShader(geometryShader);
    setPixelShader(pixelShader);
}

void StateManager11::setVertexShader(const d3d11::VertexShader *shader)
{
    ResourceSerial serial = shader ? shader->getSerial() : 0;

    if (serial != mAppliedVertexShader)
    {
        ID3D11VertexShader *appliedShader = shader ? shader->get() : nullptr;
        mRenderer->getDeviceContext()->VSSetShader(appliedShader, nullptr, 0);
        mAppliedVertexShader = serial;
    }
}

void StateManager11::setGeometryShader(const d3d11::GeometryShader *shader)
{
    ResourceSerial serial = shader ? shader->getSerial() : 0;

    if (serial != mAppliedGeometryShader)
    {
        ID3D11GeometryShader *appliedShader = shader ? shader->get() : nullptr;
        mRenderer->getDeviceContext()->GSSetShader(appliedShader, nullptr, 0);
        mAppliedGeometryShader = serial;
    }
}

void StateManager11::setPixelShader(const d3d11::PixelShader *shader)
{
    ResourceSerial serial = shader ? shader->getSerial() : 0;

    if (serial != mAppliedPixelShader)
    {
        ID3D11PixelShader *appliedShader = shader ? shader->get() : nullptr;
        mRenderer->getDeviceContext()->PSSetShader(appliedShader, nullptr, 0);
        mAppliedPixelShader = serial;
    }
}

void StateManager11::setComputeShader(const d3d11::ComputeShader *shader)
{
    ResourceSerial serial = shader ? shader->getSerial() : 0;

    if (serial != mAppliedComputeShader)
    {
        ID3D11ComputeShader *appliedShader = shader ? shader->get() : nullptr;
        mRenderer->getDeviceContext()->CSSetShader(appliedShader, nullptr, 0);
        mAppliedComputeShader = serial;
    }
}

// For each Direct3D sampler of either the pixel or vertex stage,
// looks up the corresponding OpenGL texture image unit and texture type,
// and sets the texture and its addressing/filtering state (or NULL when inactive).
// Sampler mapping needs to be up-to-date on the program object before this is called.
gl::Error StateManager11::applyTextures(const gl::Context *context,
                                        gl::SamplerType shaderType,
                                        const FramebufferTextureArray &framebufferTextures,
                                        size_t framebufferTextureCount)
{
    const auto &glState    = context->getGLState();
    const auto &caps       = context->getCaps();
    ProgramD3D *programD3D = GetImplAs<ProgramD3D>(glState.getProgram());

    ASSERT(!programD3D->isSamplerMappingDirty());

    // TODO(jmadill): Use the Program's sampler bindings.

    unsigned int samplerRange = programD3D->getUsedSamplerRange(shaderType);
    for (unsigned int samplerIndex = 0; samplerIndex < samplerRange; samplerIndex++)
    {
        GLenum textureType = programD3D->getSamplerTextureType(shaderType, samplerIndex);
        GLint textureUnit  = programD3D->getSamplerMapping(shaderType, samplerIndex, caps);
        if (textureUnit != -1)
        {
            gl::Texture *texture = glState.getSamplerTexture(textureUnit, textureType);
            ASSERT(texture);

            gl::Sampler *samplerObject = glState.getSampler(textureUnit);

            const gl::SamplerState &samplerState =
                samplerObject ? samplerObject->getSamplerState() : texture->getSamplerState();

            // TODO: std::binary_search may become unavailable using older versions of GCC
            if (texture->getTextureState().isSamplerComplete(samplerState,
                                                             context->getContextState()) &&
                !std::binary_search(framebufferTextures.begin(),
                                    framebufferTextures.begin() + framebufferTextureCount, texture))
            {
                ANGLE_TRY(
                    setSamplerState(context, shaderType, samplerIndex, texture, samplerState));
                ANGLE_TRY(setTexture(context, shaderType, samplerIndex, texture));
            }
            else
            {
                // Texture is not sampler complete or it is in use by the framebuffer.  Bind the
                // incomplete texture.
                gl::Texture *incompleteTexture =
                    mRenderer->getIncompleteTexture(context, textureType);

                ANGLE_TRY(setSamplerState(context, shaderType, samplerIndex, incompleteTexture,
                                          incompleteTexture->getSamplerState()));
                ANGLE_TRY(setTexture(context, shaderType, samplerIndex, incompleteTexture));
            }
        }
        else
        {
            // No texture bound to this slot even though it is used by the shader, bind a NULL
            // texture
            ANGLE_TRY(setTexture(context, shaderType, samplerIndex, nullptr));
        }
    }

    // Set all the remaining textures to NULL
    size_t samplerCount = (shaderType == gl::SAMPLER_PIXEL) ? caps.maxTextureImageUnits
                                                            : caps.maxVertexTextureImageUnits;
    clearTextures(shaderType, samplerRange, samplerCount);

    return gl::NoError();
}

gl::Error StateManager11::syncTextures(const gl::Context *context)
{
    FramebufferTextureArray framebufferTextures;
    size_t framebufferSerialCount =
        mRenderer->getBoundFramebufferTextures(context->getContextState(), &framebufferTextures);

    ANGLE_TRY(
        applyTextures(context, gl::SAMPLER_VERTEX, framebufferTextures, framebufferSerialCount));
    ANGLE_TRY(
        applyTextures(context, gl::SAMPLER_PIXEL, framebufferTextures, framebufferSerialCount));
    return gl::NoError();
}

gl::Error StateManager11::setSamplerState(const gl::Context *context,
                                          gl::SamplerType type,
                                          int index,
                                          gl::Texture *texture,
                                          const gl::SamplerState &samplerState)
{
#if !defined(NDEBUG)
    // Storage should exist, texture should be complete. Only verified in Debug.
    TextureD3D *textureD3D  = GetImplAs<TextureD3D>(texture);
    TextureStorage *storage = nullptr;
    ANGLE_TRY(textureD3D->getNativeTexture(context, &storage));
    ASSERT(storage);
#endif  // !defined(NDEBUG)

    // Sampler metadata that's passed to shaders in uniforms is stored separately from rest of the
    // sampler state since having it in contiguous memory makes it possible to memcpy to a constant
    // buffer, and it doesn't affect the state set by PSSetSamplers/VSSetSamplers.
    SamplerMetadata11 *metadata = nullptr;

    auto *deviceContext = mRenderer->getDeviceContext();

    if (type == gl::SAMPLER_PIXEL)
    {
        ASSERT(static_cast<unsigned int>(index) < mRenderer->getNativeCaps().maxTextureImageUnits);

        if (mForceSetPixelSamplerStates[index] ||
            memcmp(&samplerState, &mCurPixelSamplerStates[index], sizeof(gl::SamplerState)) != 0)
        {
            ID3D11SamplerState *dxSamplerState = nullptr;
            ANGLE_TRY(mRenderer->getSamplerState(samplerState, &dxSamplerState));

            ASSERT(dxSamplerState != nullptr);
            deviceContext->PSSetSamplers(index, 1, &dxSamplerState);

            mCurPixelSamplerStates[index] = samplerState;
        }

        mForceSetPixelSamplerStates[index] = false;

        metadata = &mSamplerMetadataPS;
    }
    else if (type == gl::SAMPLER_VERTEX)
    {
        ASSERT(static_cast<unsigned int>(index) <
               mRenderer->getNativeCaps().maxVertexTextureImageUnits);

        if (mForceSetVertexSamplerStates[index] ||
            memcmp(&samplerState, &mCurVertexSamplerStates[index], sizeof(gl::SamplerState)) != 0)
        {
            ID3D11SamplerState *dxSamplerState = nullptr;
            ANGLE_TRY(mRenderer->getSamplerState(samplerState, &dxSamplerState));

            ASSERT(dxSamplerState != nullptr);
            deviceContext->VSSetSamplers(index, 1, &dxSamplerState);

            mCurVertexSamplerStates[index] = samplerState;
        }

        mForceSetVertexSamplerStates[index] = false;

        metadata = &mSamplerMetadataVS;
    }
    else if (type == gl::SAMPLER_COMPUTE)
    {
        ASSERT(static_cast<unsigned int>(index) <
               mRenderer->getNativeCaps().maxComputeTextureImageUnits);

        if (mForceSetComputeSamplerStates[index] ||
            memcmp(&samplerState, &mCurComputeSamplerStates[index], sizeof(gl::SamplerState)) != 0)
        {
            ID3D11SamplerState *dxSamplerState = nullptr;
            ANGLE_TRY(mRenderer->getSamplerState(samplerState, &dxSamplerState));

            ASSERT(dxSamplerState != nullptr);
            deviceContext->CSSetSamplers(index, 1, &dxSamplerState);

            mCurComputeSamplerStates[index] = samplerState;
        }

        mForceSetComputeSamplerStates[index] = false;

        metadata = &mSamplerMetadataCS;
    }
    else
        UNREACHABLE();

    ASSERT(metadata != nullptr);
    metadata->update(index, *texture);

    return gl::NoError();
}

gl::Error StateManager11::setTexture(const gl::Context *context,
                                     gl::SamplerType type,
                                     int index,
                                     gl::Texture *texture)
{
    const d3d11::SharedSRV *textureSRV = nullptr;

    if (texture)
    {
        TextureD3D *textureImpl = GetImplAs<TextureD3D>(texture);

        TextureStorage *texStorage = nullptr;
        ANGLE_TRY(textureImpl->getNativeTexture(context, &texStorage));

        // Texture should be complete and have a storage
        ASSERT(texStorage);

        TextureStorage11 *storage11 = GetAs<TextureStorage11>(texStorage);

        ANGLE_TRY(storage11->getSRV(context, texture->getTextureState(), &textureSRV));

        // If we get an invalid SRV here, something went wrong in the texture class and we're
        // unexpectedly missing the shader resource view.
        ASSERT(textureSRV->valid());

        textureImpl->resetDirty();
    }

    ASSERT(
        (type == gl::SAMPLER_PIXEL &&
         static_cast<unsigned int>(index) < mRenderer->getNativeCaps().maxTextureImageUnits) ||
        (type == gl::SAMPLER_VERTEX &&
         static_cast<unsigned int>(index) < mRenderer->getNativeCaps().maxVertexTextureImageUnits));

    setShaderResource(type, index, textureSRV->get());
    return gl::NoError();
}

gl::Error StateManager11::syncProgram(const gl::Context *context, GLenum drawMode)
{
    const auto &glState = context->getGLState();
    const auto *va11    = GetImplAs<VertexArray11>(glState.getVertexArray());
    auto *programD3D    = GetImplAs<ProgramD3D>(glState.getProgram());

    programD3D->updateCachedInputLayout(va11->getCurrentStateSerial(), glState);

    // Binaries must be compiled before the sync.
    ASSERT(programD3D->hasVertexExecutableForCachedInputLayout());
    ASSERT(programD3D->hasGeometryExecutableForPrimitiveType(drawMode));
    ASSERT(programD3D->hasPixelExecutableForCachedOutputLayout());

    ShaderExecutableD3D *vertexExe = nullptr;
    ANGLE_TRY(programD3D->getVertexExecutableForCachedInputLayout(&vertexExe, nullptr));

    ShaderExecutableD3D *pixelExe = nullptr;
    ANGLE_TRY(programD3D->getPixelExecutableForCachedOutputLayout(&pixelExe, nullptr));

    ShaderExecutableD3D *geometryExe = nullptr;
    ANGLE_TRY(programD3D->getGeometryExecutableForPrimitiveType(context->getContextState(),
                                                                drawMode, &geometryExe, nullptr));

    const d3d11::VertexShader *vertexShader =
        (vertexExe ? &GetAs<ShaderExecutable11>(vertexExe)->getVertexShader() : nullptr);

    // Skip pixel shader if we're doing rasterizer discard.
    const d3d11::PixelShader *pixelShader = nullptr;
    if (!glState.getRasterizerState().rasterizerDiscard)
    {
        pixelShader = (pixelExe ? &GetAs<ShaderExecutable11>(pixelExe)->getPixelShader() : nullptr);
    }

    const d3d11::GeometryShader *geometryShader = nullptr;
    if (glState.isTransformFeedbackActiveUnpaused())
    {
        geometryShader =
            (vertexExe ? &GetAs<ShaderExecutable11>(vertexExe)->getStreamOutShader() : nullptr);
    }
    else
    {
        geometryShader =
            (geometryExe ? &GetAs<ShaderExecutable11>(geometryExe)->getGeometryShader() : nullptr);
    }

    setDrawShaders(vertexShader, geometryShader, pixelShader);
    return gl::NoError();
}

gl::Error StateManager11::applyVertexBuffer(const gl::Context *context,
                                            GLenum mode,
                                            GLint first,
                                            GLsizei count,
                                            GLsizei instances,
                                            TranslatedIndexData *indexInfo)
{
    const auto &state       = context->getGLState();
    const auto &vertexArray = state.getVertexArray();
    auto *vertexArray11     = GetImplAs<VertexArray11>(vertexArray);

    if (vertexArray11->hasDirtyOrDynamicAttrib(context))
    {
        ANGLE_TRY(vertexArray11->updateDirtyAndDynamicAttribs(context, &mVertexDataManager, first,
                                                              count, instances));
        invalidateVertexBuffer();
    }

    ANGLE_TRY(syncCurrentValueAttribs(state));

    // If index information is passed, mark it with the current changed status.
    if (indexInfo)
    {
        indexInfo->srcIndexData.srcIndicesChanged = mAppliedIBChanged;
    }

    if (!mLastFirstVertex.valid() || mLastFirstVertex.value() != first)
    {
        mLastFirstVertex    = first;
        mInputLayoutIsDirty = true;
    }

    // Currently buffer data updates sometimes won't get flushed if we short-circuit.
    // TODO(jmadill): Re-enable once we fix updates.
    //if (!mInputLayoutIsDirty)
    //{
    //    return gl::NoError();
    //}

    GLsizei numIndicesPerInstance = 0;
    if (instances > 0)
    {
        numIndicesPerInstance = count;
    }

    const auto &vertexArrayAttribs = vertexArray11->getTranslatedAttribs();
    gl::Program *program           = state.getProgram();

    // Sort the attributes according to ensure we re-use similar input layouts.
    AttribIndexArray sortedSemanticIndices;
    SortAttributesByLayout(program, vertexArrayAttribs, mCurrentValueAttribs,
                           &sortedSemanticIndices, &mCurrentAttributes);

    auto featureLevel = mRenderer->getRenderer11DeviceCaps().featureLevel;

    // If we are using FL 9_3, make sure the first attribute is not instanced
    if (featureLevel <= D3D_FEATURE_LEVEL_9_3 && !mCurrentAttributes.empty())
    {
        if (mCurrentAttributes[0]->divisor > 0)
        {
            Optional<size_t> firstNonInstancedIndex = FindFirstNonInstanced(mCurrentAttributes);
            if (firstNonInstancedIndex.valid())
            {
                size_t index = firstNonInstancedIndex.value();
                std::swap(mCurrentAttributes[0], mCurrentAttributes[index]);
                std::swap(sortedSemanticIndices[0], sortedSemanticIndices[index]);
            }
        }
    }

    // Update the applied input layout by querying the cache.
    ANGLE_TRY(mInputLayoutCache.updateInputLayout(mRenderer, state, mCurrentAttributes, mode,
                                                  sortedSemanticIndices, numIndicesPerInstance));

    // Update the applied vertex buffers.
    ANGLE_TRY(mInputLayoutCache.applyVertexBuffers(mRenderer, state, mCurrentAttributes, mode,
                                                   first, indexInfo));

    // InputLayoutCache::applyVertexBuffers calls through to the Bufer11 to get the native vertex
    // buffer (ID3D11Buffer *). Because we allocate these buffers lazily, this will trigger
    // allocation. This in turn will signal that the buffer is dirty. Since we just resolved the
    // dirty-ness in VertexArray11::updateDirtyAndDynamicAttribs, this can make us do a needless
    // update on the second draw call.
    // Hence we clear the flags here, after we've applied vertex data, since we know everything
    // is clean. This is a bit of a hack.
    vertexArray11->clearDirtyAndPromoteDynamicAttribs(state, count);

    mInputLayoutIsDirty = false;
    return gl::NoError();
}

gl::Error StateManager11::applyIndexBuffer(const gl::ContextState &data,
                                           const void *indices,
                                           GLsizei count,
                                           GLenum type,
                                           TranslatedIndexData *indexInfo)
{
    const auto &glState            = data.getState();
    gl::VertexArray *vao           = glState.getVertexArray();
    gl::Buffer *elementArrayBuffer = vao->getElementArrayBuffer().get();
    ANGLE_TRY(mIndexDataManager.prepareIndexData(type, count, elementArrayBuffer, indices,
                                                 indexInfo, glState.isPrimitiveRestartEnabled()));

    ID3D11Buffer *buffer = nullptr;
    DXGI_FORMAT bufferFormat =
        (indexInfo->indexType == GL_UNSIGNED_INT) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    if (indexInfo->storage)
    {
        Buffer11 *storage = GetAs<Buffer11>(indexInfo->storage);
        ANGLE_TRY_RESULT(storage->getBuffer(BUFFER_USAGE_INDEX), buffer);
    }
    else
    {
        IndexBuffer11 *indexBuffer = GetAs<IndexBuffer11>(indexInfo->indexBuffer);
        buffer                     = indexBuffer->getBuffer().get();
    }

    mAppliedIBChanged = false;
    setIndexBuffer(buffer, bufferFormat, indexInfo->startOffset, true);

    return gl::NoError();
}

void StateManager11::setIndexBuffer(ID3D11Buffer *buffer,
                                    DXGI_FORMAT indexFormat,
                                    unsigned int offset,
                                    bool indicesChanged)
{
    if (buffer != mAppliedIB || indexFormat != mAppliedIBFormat || offset != mAppliedIBOffset)
    {
        mRenderer->getDeviceContext()->IASetIndexBuffer(buffer, indexFormat, offset);

        mAppliedIB       = buffer;
        mAppliedIBFormat = indexFormat;
        mAppliedIBOffset = offset;

        if (indicesChanged)
        {
            mAppliedIBChanged = true;
        }
    }
}

// Vertex buffer is invalidated outside this function.
gl::Error StateManager11::updateVertexOffsetsForPointSpritesEmulation(GLint startVertex,
                                                                      GLsizei emulatedInstanceId)
{
    return mInputLayoutCache.updateVertexOffsetsForPointSpritesEmulation(
        mRenderer, mCurrentAttributes, startVertex, emulatedInstanceId);
}

}  // namespace rx

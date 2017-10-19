/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrContext.h"
#include "GrCaps.h"
#include "GrGpu.h"
#include "GrResourceKey.h"
#include "GrRenderTarget.h"
#include "GrTexture.h"
#include "GrTexturePriv.h"
#include "GrTypes.h"
#include "SkMath.h"
#include "SkMipMap.h"
#include "SkTypes.h"

void GrTexture::dirtyMipMaps(bool mipMapsDirty) {
    if (mipMapsDirty) {
        if (kValid_MipMapsStatus == fMipMapsStatus) {
            fMipMapsStatus = kAllocated_MipMapsStatus;
        }
    } else {
        const bool sizeChanged = kNotAllocated_MipMapsStatus == fMipMapsStatus;
        fMipMapsStatus = kValid_MipMapsStatus;
        if (sizeChanged) {
            // This must not be called until after changing fMipMapsStatus.
            this->didChangeGpuMemorySize();
            // TODO(http://skbug.com/4548) - The desc and scratch key should be
            // updated to reflect the newly-allocated mipmaps.
        }
    }
}

size_t GrTexture::onGpuMemorySize() const {
    return GrSurface::ComputeSize(this->config(), this->width(), this->height(), 1,
                                  this->texturePriv().hasMipMaps(), false);
}

/////////////////////////////////////////////////////////////////////////////
GrTexture::GrTexture(GrGpu* gpu, const GrSurfaceDesc& desc, GrSLType samplerType,
                     GrSamplerParams::FilterMode highestFilterMode, bool wasMipMapDataProvided)
    : INHERITED(gpu, desc)
    , fSamplerType(samplerType)
    , fHighestFilterMode(highestFilterMode)
    // Mip color mode is explicitly set after creation via GrTexturePriv
    , fMipColorMode(SkDestinationSurfaceColorMode::kLegacy) {
    if (wasMipMapDataProvided) {
        fMipMapsStatus = kValid_MipMapsStatus;
        fMaxMipMapLevel = SkMipMap::ComputeLevelCount(this->width(), this->height());
    } else {
        fMipMapsStatus = kNotAllocated_MipMapsStatus;
        fMaxMipMapLevel = 0;
    }
}

void GrTexture::computeScratchKey(GrScratchKey* key) const {
    const GrRenderTarget* rt = this->asRenderTarget();
    int sampleCount = 0;
    if (rt) {
        sampleCount = rt->numStencilSamples();
    }
    GrTexturePriv::ComputeScratchKey(this->config(), this->width(), this->height(),
                                     SkToBool(rt), sampleCount,
                                     this->texturePriv().hasMipMaps(), key);
}

void GrTexturePriv::ComputeScratchKey(GrPixelConfig config, int width, int height,
                                      bool isRenderTarget, int sampleCnt,
                                      bool isMipMapped, GrScratchKey* key) {
    static const GrScratchKey::ResourceType kType = GrScratchKey::GenerateResourceType();
    uint32_t flags = isRenderTarget;

    SkASSERT(0 == sampleCnt || isRenderTarget);

    // make sure desc.fConfig fits in 5 bits
    SkASSERT(sk_float_log2(kLast_GrPixelConfig) <= 5);
    SkASSERT(static_cast<int>(config) < (1 << 5));
    SkASSERT(sampleCnt < (1 << 8));
    SkASSERT(flags < (1 << 10));

    GrScratchKey::Builder builder(key, kType, 3);
    builder[0] = width;
    builder[1] = height;
    builder[2] = config | (isMipMapped << 5) | (sampleCnt << 6) | (flags << 14);
}

void GrTexturePriv::ComputeScratchKey(const GrSurfaceDesc& desc, GrScratchKey* key) {
    // Note: the fOrigin field is not used in the scratch key
    return ComputeScratchKey(desc.fConfig, desc.fWidth, desc.fHeight,
                             SkToBool(desc.fFlags & kRenderTarget_GrSurfaceFlag), desc.fSampleCnt,
                             desc.fIsMipMapped, key);
}

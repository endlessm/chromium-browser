/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrSurfaceProxy.h"
#include "GrSurfaceProxyPriv.h"

#include "GrCaps.h"
#include "GrContext.h"
#include "GrContextPriv.h"
#include "GrGpuResourcePriv.h"
#include "GrOpList.h"
#include "GrResourceProvider.h"
#include "GrSurfaceContext.h"
#include "GrTexturePriv.h"
#include "GrTextureRenderTargetProxy.h"

#include "SkMathPriv.h"

GrSurfaceProxy::GrSurfaceProxy(sk_sp<GrSurface> surface, GrSurfaceOrigin origin, SkBackingFit fit)
        : INHERITED(std::move(surface))
        , fConfig(fTarget->config())
        , fWidth(fTarget->width())
        , fHeight(fTarget->height())
        , fOrigin(origin)
        , fFit(fit)
        , fBudgeted(fTarget->resourcePriv().isBudgeted())
        , fFlags(0)
        , fUniqueID(fTarget->uniqueID())  // Note: converting from unique resource ID to a proxy ID!
        , fNeedsClear(false)
        , fGpuMemorySize(kInvalidGpuMemorySize)
        , fLastOpList(nullptr) {
}

GrSurfaceProxy::~GrSurfaceProxy() {
    // For this to be deleted the opList that held a ref on it (if there was one) must have been
    // deleted. Which would have cleared out this back pointer.
    SkASSERT(!fLastOpList);
}

static bool attach_stencil_if_needed(GrResourceProvider* resourceProvider,
                                     GrSurface* surface, bool needsStencil) {
    if (needsStencil) {
        GrRenderTarget* rt = surface->asRenderTarget();
        if (!rt) {
            SkASSERT(0);
            return false;
        }

        if (!resourceProvider->attachStencilAttachment(rt)) {
            return false;
        }
    }

    return true;
}

sk_sp<GrSurface> GrSurfaceProxy::createSurfaceImpl(
                                                GrResourceProvider* resourceProvider,
                                                int sampleCnt, bool needsStencil,
                                                GrSurfaceFlags flags, bool isMipMapped,
                                                SkDestinationSurfaceColorMode mipColorMode) const {

    GrSurfaceDesc desc;
    desc.fFlags = flags;
    if (fNeedsClear) {
        desc.fFlags |= kPerformInitialClear_GrSurfaceFlag;
    }
    desc.fOrigin = fOrigin;
    desc.fWidth = fWidth;
    desc.fHeight = fHeight;
    desc.fConfig = fConfig;
    desc.fSampleCnt = sampleCnt;
    desc.fIsMipMapped = isMipMapped;

    sk_sp<GrSurface> surface;
    if (SkBackingFit::kApprox == fFit) {
        surface.reset(resourceProvider->createApproxTexture(desc, fFlags).release());
    } else {
        surface.reset(resourceProvider->createTexture(desc, fBudgeted, fFlags).release());
    }
    if (!surface) {
        return nullptr;
    }

    surface->asTexture()->texturePriv().setMipColorMode(mipColorMode);

    if (!attach_stencil_if_needed(resourceProvider, surface.get(), needsStencil)) {
        return nullptr;
    }

    return surface;
}

void GrSurfaceProxy::assign(sk_sp<GrSurface> surface) {
    SkASSERT(!fTarget && surface);
    fTarget = surface.release();
    this->INHERITED::transferRefs();

#ifdef SK_DEBUG
    if (kInvalidGpuMemorySize != this->getRawGpuMemorySize_debugOnly()) {
        SkASSERT(fTarget->gpuMemorySize() <= this->getRawGpuMemorySize_debugOnly());
    }
#endif
}

bool GrSurfaceProxy::instantiateImpl(GrResourceProvider* resourceProvider, int sampleCnt,
                                     bool needsStencil, GrSurfaceFlags flags, bool isMipMapped,
                                     SkDestinationSurfaceColorMode mipColorMode) {
    if (fTarget) {
        return attach_stencil_if_needed(resourceProvider, fTarget, needsStencil);
    }

    sk_sp<GrSurface> surface = this->createSurfaceImpl(resourceProvider, sampleCnt, needsStencil,
                                                       flags, isMipMapped, mipColorMode);
    if (!surface) {
        return false;
    }

    this->assign(std::move(surface));
    return true;
}

void GrSurfaceProxy::computeScratchKey(GrScratchKey* key) const {
    const GrRenderTargetProxy* rtp = this->asRenderTargetProxy();
    int sampleCount = 0;
    if (rtp) {
        sampleCount = rtp->numStencilSamples();
    }

    const GrTextureProxy* tp = this->asTextureProxy();
    bool hasMipMaps = false;
    if (tp) {
        hasMipMaps = tp->isMipMapped();
    }

    int width = this->width();
    int height = this->height();
    if (SkBackingFit::kApprox == fFit) {
        // bin by pow2 with a reasonable min
        width  = SkTMax(GrResourceProvider::kMinScratchTextureSize, GrNextPow2(width));
        height = SkTMax(GrResourceProvider::kMinScratchTextureSize, GrNextPow2(height));
    }

    GrTexturePriv::ComputeScratchKey(this->config(), width, height, SkToBool(rtp), sampleCount,
                                     hasMipMaps, key);
}

void GrSurfaceProxy::setLastOpList(GrOpList* opList) {
#ifdef SK_DEBUG
    if (fLastOpList) {
        SkASSERT(fLastOpList->isClosed());
    }
#endif

    // Un-reffed
    fLastOpList = opList;
}

GrRenderTargetOpList* GrSurfaceProxy::getLastRenderTargetOpList() {
    return fLastOpList ? fLastOpList->asRenderTargetOpList() : nullptr;
}

GrTextureOpList* GrSurfaceProxy::getLastTextureOpList() {
    return fLastOpList ? fLastOpList->asTextureOpList() : nullptr;
}

sk_sp<GrSurfaceProxy> GrSurfaceProxy::MakeWrapped(sk_sp<GrSurface> surf, GrSurfaceOrigin origin) {
    if (!surf) {
        return nullptr;
    }

    if (surf->asTexture()) {
        if (surf->asRenderTarget()) {
            return sk_sp<GrSurfaceProxy>(new GrTextureRenderTargetProxy(std::move(surf), origin));
        } else {
            return sk_sp<GrSurfaceProxy>(new GrTextureProxy(std::move(surf), origin));
        }
    } else {
        SkASSERT(surf->asRenderTarget());

        // Not texturable
        return sk_sp<GrSurfaceProxy>(new GrRenderTargetProxy(std::move(surf), origin));
    }
}

sk_sp<GrTextureProxy> GrSurfaceProxy::MakeWrapped(sk_sp<GrTexture> tex, GrSurfaceOrigin origin) {
    if (!tex) {
        return nullptr;
    }

    if (tex->asRenderTarget()) {
        return sk_sp<GrTextureProxy>(new GrTextureRenderTargetProxy(std::move(tex), origin));
    } else {
        return sk_sp<GrTextureProxy>(new GrTextureProxy(std::move(tex), origin));
    }
}

sk_sp<GrTextureProxy> GrSurfaceProxy::MakeDeferred(GrResourceProvider* resourceProvider,
                                                   const GrSurfaceDesc& desc,
                                                   SkBackingFit fit,
                                                   SkBudgeted budgeted,
                                                   uint32_t flags) {
    SkASSERT(0 == flags || GrResourceProvider::kNoPendingIO_Flag == flags);

    const GrCaps* caps = resourceProvider->caps();

    // TODO: move this logic into GrResourceProvider!
    // TODO: share this testing code with check_texture_creation_params
    if (!caps->isConfigTexturable(desc.fConfig)) {
        return nullptr;
    }

    bool willBeRT = SkToBool(desc.fFlags & kRenderTarget_GrSurfaceFlag);
    if (willBeRT && !caps->isConfigRenderable(desc.fConfig, desc.fSampleCnt > 0)) {
        return nullptr;
    }

    // We currently do not support multisampled textures
    if (!willBeRT && desc.fSampleCnt > 0) {
        return nullptr;
    }

    int maxSize;
    if (willBeRT) {
        maxSize = caps->maxRenderTargetSize();
    } else {
        maxSize = caps->maxTextureSize();
    }

    if (desc.fWidth > maxSize || desc.fHeight > maxSize || desc.fWidth <= 0 || desc.fHeight <= 0) {
        return nullptr;
    }

    GrSurfaceDesc copyDesc = desc;
    copyDesc.fSampleCnt = caps->getSampleCount(desc.fSampleCnt, desc.fConfig);

    // Temporarily force instantiation for crbug.com/769760 and crbug.com/769898
    sk_sp<GrTexture> tex;

    if (SkBackingFit::kApprox == fit) {
        tex = resourceProvider->createApproxTexture(copyDesc, flags);
    } else {
        tex = resourceProvider->createTexture(copyDesc, budgeted, flags);
    }

    if (!tex) {
        return nullptr;
    }

    return GrSurfaceProxy::MakeWrapped(std::move(tex), copyDesc.fOrigin);
}

sk_sp<GrTextureProxy> GrSurfaceProxy::MakeDeferred(GrResourceProvider* resourceProvider,
                                                   const GrSurfaceDesc& desc,
                                                   SkBudgeted budgeted,
                                                   const void* srcData,
                                                   size_t rowBytes) {
    if (srcData) {
        GrMipLevel mipLevel = { srcData, rowBytes };

        return resourceProvider->createTextureProxy(desc, budgeted, mipLevel);
    }

    return GrSurfaceProxy::MakeDeferred(resourceProvider, desc, SkBackingFit::kExact, budgeted);
}

sk_sp<GrTextureProxy> GrSurfaceProxy::MakeDeferredMipMap(
                                                    GrResourceProvider* resourceProvider,
                                                    const GrSurfaceDesc& desc,
                                                    SkBudgeted budgeted,
                                                    const GrMipLevel texels[],
                                                    int mipLevelCount,
                                                    SkDestinationSurfaceColorMode mipColorMode) {
    if (!mipLevelCount) {
        if (texels) {
            return nullptr;
        }
        return GrSurfaceProxy::MakeDeferred(resourceProvider, desc, budgeted, nullptr, 0);
    } else if (1 == mipLevelCount) {
        if (!texels) {
            return nullptr;
        }
        return resourceProvider->createTextureProxy(desc, budgeted, texels[0]);
    }

    for (int i = 0; i < mipLevelCount; ++i) {
        if (!texels[i].fPixels) {
            return nullptr;
        }
    }

    sk_sp<GrTexture> tex(resourceProvider->createTexture(desc, budgeted,
                                                         texels, mipLevelCount, mipColorMode));
    if (!tex) {
        return nullptr;
    }

    return GrSurfaceProxy::MakeWrapped(std::move(tex), desc.fOrigin);
}

sk_sp<GrTextureProxy> GrSurfaceProxy::MakeWrappedBackend(GrContext* context,
                                                         GrBackendTexture& backendTex,
                                                         GrSurfaceOrigin origin) {
    sk_sp<GrTexture> tex(context->resourceProvider()->wrapBackendTexture(backendTex));
    return GrSurfaceProxy::MakeWrapped(std::move(tex), origin);
}

#ifdef SK_DEBUG
void GrSurfaceProxy::validate(GrContext* context) const {
    if (fTarget) {
        SkASSERT(fTarget->getContext() == context);
    }

    INHERITED::validate();
}
#endif

sk_sp<GrTextureProxy> GrSurfaceProxy::Copy(GrContext* context,
                                           GrSurfaceProxy* src,
                                           SkIRect srcRect,
                                           SkBudgeted budgeted) {
    if (!srcRect.intersect(SkIRect::MakeWH(src->width(), src->height()))) {
        return nullptr;
    }

    GrSurfaceDesc dstDesc;
    dstDesc.fOrigin = src->origin();
    dstDesc.fWidth = srcRect.width();
    dstDesc.fHeight = srcRect.height();
    dstDesc.fConfig = src->config();

    sk_sp<GrSurfaceContext> dstContext(context->contextPriv().makeDeferredSurfaceContext(
                                                                            dstDesc,
                                                                            SkBackingFit::kExact,
                                                                            budgeted));
    if (!dstContext) {
        return nullptr;
    }

    if (!dstContext->copy(src, srcRect, SkIPoint::Make(0, 0))) {
        return nullptr;
    }

    return dstContext->asTextureProxyRef();
}

sk_sp<GrTextureProxy> GrSurfaceProxy::Copy(GrContext* context, GrSurfaceProxy* src,
                                           SkBudgeted budgeted) {
    return Copy(context, src, SkIRect::MakeWH(src->width(), src->height()), budgeted);
}

sk_sp<GrSurfaceContext> GrSurfaceProxy::TestCopy(GrContext* context, const GrSurfaceDesc& dstDesc,
                                                 GrSurfaceProxy* srcProxy) {

    sk_sp<GrSurfaceContext> dstContext(context->contextPriv().makeDeferredSurfaceContext(
                                                                            dstDesc,
                                                                            SkBackingFit::kExact,
                                                                            SkBudgeted::kYes));
    if (!dstContext) {
        return nullptr;
    }

    if (!dstContext->copy(srcProxy)) {
        return nullptr;
    }

    return dstContext;
}

void GrSurfaceProxyPriv::exactify() {
    if (this->isExact()) {
        return;
    }

    SkASSERT(SkBackingFit::kApprox == fProxy->fFit);

    if (fProxy->fTarget) {
        // The kApprox but already instantiated case. Setting the proxy's width & height to
        // the instantiated width & height could have side-effects going forward, since we're
        // obliterating the area of interest information. This call (exactify) only used 
        // when converting an SkSpecialImage to an SkImage so the proxy shouldn't be
        // used for additional draws.
        fProxy->fWidth = fProxy->fTarget->width();
        fProxy->fHeight = fProxy->fTarget->height();
        return;
    }

    // The kApprox uninstantiated case. Making this proxy be exact should be okay.
    // It could mess things up if prior decisions were based on the approximate size.
    fProxy->fFit = SkBackingFit::kExact;
    // If fGpuMemorySize is used when caching specialImages for the image filter DAG. If it has
    // already been computed we want to leave it alone so that amount will be removed when
    // the special image goes away. If it hasn't been computed yet it might as well compute the
    // exact amount.
}


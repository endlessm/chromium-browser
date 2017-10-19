/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrRenderTargetOpList.h"
#include "GrAuditTrail.h"
#include "GrCaps.h"
#include "GrGpu.h"
#include "GrGpuCommandBuffer.h"
#include "GrRect.h"
#include "GrRenderTargetContext.h"
#include "instanced/InstancedRendering.h"
#include "ops/GrClearOp.h"
#include "ops/GrCopySurfaceOp.h"
#include "SkTraceEvent.h"

using gr_instanced::InstancedRendering;

////////////////////////////////////////////////////////////////////////////////

// Experimentally we have found that most combining occurs within the first 10 comparisons.
static const int kMaxOpLookback = 10;
static const int kMaxOpLookahead = 10;

GrRenderTargetOpList::GrRenderTargetOpList(GrRenderTargetProxy* proxy, GrGpu* gpu,
                                           GrAuditTrail* auditTrail)
        : INHERITED(gpu->getContext()->resourceProvider(), proxy, auditTrail)
        , fLastClipStackGenID(SK_InvalidUniqueID)
        SkDEBUGCODE(, fNumClips(0)) {
    if (GrCaps::InstancedSupport::kNone != gpu->caps()->instancedSupport()) {
        fInstancedRendering.reset(gpu->createInstancedRendering());
    }
}

GrRenderTargetOpList::~GrRenderTargetOpList() {
}

////////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
void GrRenderTargetOpList::dump() const {
    INHERITED::dump();

    SkDebugf("ops (%d):\n", fRecordedOps.count());
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        SkDebugf("*******************************\n");
        if (!fRecordedOps[i].fOp) {
            SkDebugf("%d: <combined forward>\n", i);
        } else {
            SkDebugf("%d: %s\n", i, fRecordedOps[i].fOp->name());
            SkString str = fRecordedOps[i].fOp->dumpInfo();
            SkDebugf("%s\n", str.c_str());
            const SkRect& bounds = fRecordedOps[i].fOp->bounds();
            SkDebugf("ClippedBounds: [L: %.2f, T: %.2f, R: %.2f, B: %.2f]\n", bounds.fLeft,
                     bounds.fTop, bounds.fRight, bounds.fBottom);
        }
    }
}
#endif

void GrRenderTargetOpList::onPrepare(GrOpFlushState* flushState) {
    SkASSERT(fTarget.get()->priv().peekRenderTarget());
    SkASSERT(this->isClosed());
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    TRACE_EVENT0("skia", TRACE_FUNC);
#endif

    // Loop over the ops that haven't yet been prepared.
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        if (fRecordedOps[i].fOp) {
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
            TRACE_EVENT0("skia", fRecordedOps[i].fOp->name());
#endif
            GrOpFlushState::DrawOpArgs opArgs = {
                fTarget.get()->asRenderTargetProxy(),
                fRecordedOps[i].fAppliedClip,
                fRecordedOps[i].fDstProxy
            };

            flushState->setDrawOpArgs(&opArgs);
            fRecordedOps[i].fOp->prepare(flushState);
            flushState->setDrawOpArgs(nullptr);
        }
    }

    if (fInstancedRendering) {
        fInstancedRendering->beginFlush(flushState->resourceProvider());
    }
}

static std::unique_ptr<GrGpuRTCommandBuffer> create_command_buffer(GrGpu* gpu,
                                                                   GrRenderTarget* rt,
                                                                   GrSurfaceOrigin origin,
                                                                   GrLoadOp colorLoadOp,
                                                                   GrColor loadClearColor,
                                                                   GrLoadOp stencilLoadOp) {
    const GrGpuRTCommandBuffer::LoadAndStoreInfo kColorLoadStoreInfo {
        colorLoadOp,
        GrStoreOp::kStore,
        loadClearColor
    };

    // TODO:
    // We would like to (at this level) only ever clear & discard. We would need
    // to stop splitting up higher level opLists for copyOps to achieve that.
    // Note: we would still need SB loads and stores but they would happen at a
    // lower level (inside the VK command buffer).
    const GrGpuRTCommandBuffer::StencilLoadAndStoreInfo stencilLoadAndStoreInfo {
        stencilLoadOp,
        GrStoreOp::kStore,
    };

    std::unique_ptr<GrGpuRTCommandBuffer> buffer(
                            gpu->createCommandBuffer(rt, origin,
                                                     kColorLoadStoreInfo,
                                                     stencilLoadAndStoreInfo));
    return buffer;
}

static inline void finish_command_buffer(GrGpuRTCommandBuffer* buffer) {
    if (!buffer) {
        return;
    }

    buffer->end();
    buffer->submit();
}

// TODO: this is where GrOp::renderTarget is used (which is fine since it
// is at flush time). However, we need to store the RenderTargetProxy in the
// Ops and instantiate them here.
bool GrRenderTargetOpList::onExecute(GrOpFlushState* flushState) {
    if (0 == fRecordedOps.count() && GrLoadOp::kClear != fColorLoadOp) {
        return false;
    }

    SkASSERT(fTarget.get()->priv().peekRenderTarget());
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    TRACE_EVENT0("skia", TRACE_FUNC);
#endif

    // TODO: at the very least, we want the stencil store op to always be discard (at this
    // level). In Vulkan, sub-command buffers would still need to load & store the stencil buffer.
    std::unique_ptr<GrGpuRTCommandBuffer> commandBuffer = create_command_buffer(
                                                    flushState->gpu(),
                                                    fTarget.get()->priv().peekRenderTarget(),
                                                    fTarget.get()->origin(),
                                                    fColorLoadOp, fLoadClearColor,
                                                    fStencilLoadOp);
    flushState->setCommandBuffer(commandBuffer.get());
    commandBuffer->begin();

    // Draw all the generated geometry.
    for (int i = 0; i < fRecordedOps.count(); ++i) {
        if (!fRecordedOps[i].fOp) {
            continue;
        }
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
        TRACE_EVENT0("skia", fRecordedOps[i].fOp->name());
#endif

        GrOpFlushState::DrawOpArgs opArgs {
            fTarget.get()->asRenderTargetProxy(),
            fRecordedOps[i].fAppliedClip,
            fRecordedOps[i].fDstProxy
        };

        flushState->setDrawOpArgs(&opArgs);
        fRecordedOps[i].fOp->execute(flushState);
        flushState->setDrawOpArgs(nullptr);
    }

    finish_command_buffer(commandBuffer.get());
    flushState->setCommandBuffer(nullptr);

    return true;
}

void GrRenderTargetOpList::reset() {
    fLastClipStackGenID = SK_InvalidUniqueID;
    fRecordedOps.reset();
    if (fInstancedRendering) {
        fInstancedRendering->endFlush();
        fInstancedRendering = nullptr;
    }

    INHERITED::reset();
}

void GrRenderTargetOpList::abandonGpuResources() {
    if (fInstancedRendering) {
        fInstancedRendering->resetGpuResources(InstancedRendering::ResetType::kAbandon);
    }
}

void GrRenderTargetOpList::freeGpuResources() {
    if (fInstancedRendering) {
        fInstancedRendering->resetGpuResources(InstancedRendering::ResetType::kDestroy);
    }
}

void GrRenderTargetOpList::discard() {
    // Discard calls to in-progress opLists are ignored. Calls at the start update the
    // opLists' color & stencil load ops.
    if (this->isEmpty()) {
        fColorLoadOp = GrLoadOp::kDiscard;
        fStencilLoadOp = GrLoadOp::kDiscard;
    }
}

void GrRenderTargetOpList::fullClear(const GrCaps& caps, GrColor color) {

    // This is conservative. If the opList is marked as needing a stencil buffer then there
    // may be a prior op that writes to the stencil buffer. Although the clear will ignore the
    // stencil buffer, following draw ops may not so we can't get rid of all the preceding ops.
    // Beware! If we ever add any ops that have a side effect beyond modifying the stencil
    // buffer we will need a more elaborate tracking system (skbug.com/7002).
    if (this->isEmpty() || !fTarget.get()->asRenderTargetProxy()->needsStencil()) {
        fRecordedOps.reset();
        fColorLoadOp = GrLoadOp::kClear;
        fLoadClearColor = color;
        return;
    }

    std::unique_ptr<GrClearOp> op(GrClearOp::Make(GrFixedClip::Disabled(), color, fTarget.get()));
    if (!op) {
        return;
    }

    this->recordOp(std::move(op), caps);
}

////////////////////////////////////////////////////////////////////////////////

// This closely parallels GrTextureOpList::copySurface but renderTargetOpLists
// also store the applied clip and dest proxy with the op
bool GrRenderTargetOpList::copySurface(const GrCaps& caps,
                                       GrSurfaceProxy* dst,
                                       GrSurfaceProxy* src,
                                       const SkIRect& srcRect,
                                       const SkIPoint& dstPoint) {
    SkASSERT(dst->asRenderTargetProxy() == fTarget.get());
    std::unique_ptr<GrOp> op = GrCopySurfaceOp::Make(dst, src, srcRect, dstPoint);
    if (!op) {
        return false;
    }
#ifdef ENABLE_MDB
    this->addDependency(src);
#endif

    this->recordOp(std::move(op), caps);
    return true;
}

static inline bool can_reorder(const SkRect& a, const SkRect& b) { return !GrRectsOverlap(a, b); }

bool GrRenderTargetOpList::combineIfPossible(const RecordedOp& a, GrOp* b,
                                             const GrAppliedClip* bClip,
                                             const DstProxy* bDstProxy,
                                             const GrCaps& caps) {
    if (a.fAppliedClip) {
        if (!bClip) {
            return false;
        }
        if (*a.fAppliedClip != *bClip) {
            return false;
        }
    } else if (bClip) {
        return false;
    }
    if (bDstProxy) {
        if (a.fDstProxy != *bDstProxy) {
            return false;
        }
    } else if (a.fDstProxy.proxy()) {
        return false;
    }
    return a.fOp->combineIfPossible(b, caps);
}

void GrRenderTargetOpList::recordOp(std::unique_ptr<GrOp> op,
                                    const GrCaps& caps,
                                    GrAppliedClip* clip,
                                    const DstProxy* dstProxy) {
    SkASSERT(fTarget.get());

    // A closed GrOpList should never receive new/more ops
    SkASSERT(!this->isClosed());

    // Check if there is an op we can combine with by linearly searching back until we either
    // 1) check every op
    // 2) intersect with something
    // 3) find a 'blocker'
    GR_AUDIT_TRAIL_ADD_OP(fAuditTrail, op.get(), fTarget.get()->uniqueID());
    GrOP_INFO("opList: %d Recording (%s, opID: %u)\n"
              "\tBounds [L: %.2f, T: %.2f R: %.2f B: %.2f]\n",
               this->uniqueID(),
               op->name(),
               op->uniqueID(),
               op->bounds().fLeft, op->bounds().fTop,
               op->bounds().fRight, op->bounds().fBottom);
    GrOP_INFO(SkTabString(op->dumpInfo(), 1).c_str());
    GrOP_INFO("\tOutcome:\n");
    int maxCandidates = SkTMin(kMaxOpLookback, fRecordedOps.count());
    // If we don't have a valid destination render target then we cannot reorder.
    if (maxCandidates) {
        int i = 0;
        while (true) {
            const RecordedOp& candidate = fRecordedOps.fromBack(i);

            if (this->combineIfPossible(candidate, op.get(), clip, dstProxy, caps)) {
                GrOP_INFO("\t\tBackward: Combining with (%s, opID: %u)\n", candidate.fOp->name(),
                          candidate.fOp->uniqueID());
                GrOP_INFO("\t\t\tBackward: Combined op info:\n");
                GrOP_INFO(SkTabString(candidate.fOp->dumpInfo(), 4).c_str());
                GR_AUDIT_TRAIL_OPS_RESULT_COMBINED(fAuditTrail, candidate.fOp.get(), op.get());
                return;
            }
            // Stop going backwards if we would cause a painter's order violation.
            if (!can_reorder(fRecordedOps.fromBack(i).fOp->bounds(), op->bounds())) {
                GrOP_INFO("\t\tBackward: Intersects with (%s, opID: %u)\n", candidate.fOp->name(),
                          candidate.fOp->uniqueID());
                break;
            }
            ++i;
            if (i == maxCandidates) {
                GrOP_INFO("\t\tBackward: Reached max lookback or beginning of op array %d\n", i);
                break;
            }
        }
    } else {
        GrOP_INFO("\t\tBackward: FirstOp\n");
    }
    GR_AUDIT_TRAIL_OP_RESULT_NEW(fAuditTrail, op);
    if (clip) {
        clip = fClipAllocator.make<GrAppliedClip>(std::move(*clip));
        SkDEBUGCODE(fNumClips++;)
    }
    fRecordedOps.emplace_back(std::move(op), clip, dstProxy);
    fRecordedOps.back().fOp->wasRecorded(this);
}

void GrRenderTargetOpList::forwardCombine(const GrCaps& caps) {
    SkASSERT(!this->isClosed());

    GrOP_INFO("opList: %d ForwardCombine %d ops:\n", this->uniqueID(), fRecordedOps.count());

    for (int i = 0; i < fRecordedOps.count() - 1; ++i) {
        GrOp* op = fRecordedOps[i].fOp.get();

        int maxCandidateIdx = SkTMin(i + kMaxOpLookahead, fRecordedOps.count() - 1);
        int j = i + 1;
        while (true) {
            const RecordedOp& candidate = fRecordedOps[j];

            if (this->combineIfPossible(fRecordedOps[i], candidate.fOp.get(),
                                        candidate.fAppliedClip, &candidate.fDstProxy, caps)) {
                GrOP_INFO("\t\t%d: (%s opID: %u) -> Combining with (%s, opID: %u)\n",
                          i, op->name(), op->uniqueID(),
                          candidate.fOp->name(), candidate.fOp->uniqueID());
                GR_AUDIT_TRAIL_OPS_RESULT_COMBINED(fAuditTrail, op, candidate.fOp.get());
                fRecordedOps[j].fOp = std::move(fRecordedOps[i].fOp);
                break;
            }
            // Stop traversing if we would cause a painter's order violation.
            if (!can_reorder(fRecordedOps[j].fOp->bounds(), op->bounds())) {
                GrOP_INFO("\t\t%d: (%s opID: %u) -> Intersects with (%s, opID: %u)\n",
                          i, op->name(), op->uniqueID(),
                          candidate.fOp->name(), candidate.fOp->uniqueID());
                break;
            }
            ++j;
            if (j > maxCandidateIdx) {
                GrOP_INFO("\t\t%d: (%s opID: %u) -> Reached max lookahead or end of array\n",
                          i, op->name(), op->uniqueID());
                break;
            }
        }
    }
}


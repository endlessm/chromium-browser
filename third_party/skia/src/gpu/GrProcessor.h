/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrProcessor_DEFINED
#define GrProcessor_DEFINED

#include "../private/SkAtomics.h"
#include "GrBuffer.h"
#include "GrColor.h"
#include "GrGpuResourceRef.h"
#include "GrProcessorUnitTest.h"
#include "GrProgramElement.h"
#include "GrSamplerParams.h"
#include "GrShaderVar.h"
#include "GrSurfaceProxyPriv.h"
#include "GrTextureProxy.h"
#include "SkMath.h"
#include "SkString.h"

class GrContext;
class GrCoordTransform;
class GrInvariantOutput;
class GrResourceProvider;

/**
 * Used by processors to build their keys. It incorporates each per-processor key into a larger
 * shader key.
 */
class GrProcessorKeyBuilder {
public:
    GrProcessorKeyBuilder(SkTArray<unsigned char, true>* data) : fData(data), fCount(0) {
        SkASSERT(0 == fData->count() % sizeof(uint32_t));
    }

    void add32(uint32_t v) {
        ++fCount;
        fData->push_back_n(4, reinterpret_cast<uint8_t*>(&v));
    }

    /** Inserts count uint32_ts into the key. The returned pointer is only valid until the next
        add*() call. */
    uint32_t* SK_WARN_UNUSED_RESULT add32n(int count) {
        SkASSERT(count > 0);
        fCount += count;
        return reinterpret_cast<uint32_t*>(fData->push_back_n(4 * count));
    }

    size_t size() const { return sizeof(uint32_t) * fCount; }

private:
    SkTArray<uint8_t, true>* fData; // unowned ptr to the larger key.
    int fCount;                     // number of uint32_ts added to fData by the processor.
};

/** Provides custom shader code to the Ganesh shading pipeline. GrProcessor objects *must* be
    immutable: after being constructed, their fields may not change.

    Dynamically allocated GrProcessors are managed by a per-thread memory pool. The ref count of an
    processor must reach 0 before the thread terminates and the pool is destroyed.
 */
class GrProcessor {
public:
    virtual ~GrProcessor() = default;

    /** Human-meaningful string to identify this prcoessor; may be embedded in generated shader
        code. */
    virtual const char* name() const = 0;

    /** Human-readable dump of all information */
    virtual SkString dumpInfo() const {
        SkString str;
        str.appendf("Missing data");
        return str;
    }

    /**
     * Platform specific built-in features that a processor can request for the fragment shader.
     */
    enum RequiredFeatures {
        kNone_RequiredFeatures             = 0,
        kSampleLocations_RequiredFeature   = 1 << 0
    };

    GR_DECL_BITFIELD_OPS_FRIENDS(RequiredFeatures);

    RequiredFeatures requiredFeatures() const { return fRequiredFeatures; }

    void* operator new(size_t size);
    void operator delete(void* target);

    void* operator new(size_t size, void* placement) {
        return ::operator new(size, placement);
    }
    void operator delete(void* target, void* placement) {
        ::operator delete(target, placement);
    }

    /** Helper for down-casting to a GrProcessor subclass */
    template <typename T> const T& cast() const { return *static_cast<const T*>(this); }

    uint32_t classID() const { SkASSERT(kIllegalProcessorClassID != fClassID); return fClassID; }

protected:
    GrProcessor() : fClassID(kIllegalProcessorClassID), fRequiredFeatures(kNone_RequiredFeatures) {}

    /**
     * If the prcoessor will generate code that uses platform specific built-in features, then it
     * must call these methods from its constructor. Otherwise, requests to use these features will
     * be denied.
     */
    void setWillUseSampleLocations() { fRequiredFeatures |= kSampleLocations_RequiredFeature; }

    void combineRequiredFeatures(const GrProcessor& other) {
        fRequiredFeatures |= other.fRequiredFeatures;
    }

    template <typename PROC_SUBCLASS> void initClassID() {
         static uint32_t kClassID = GenClassID();
         fClassID = kClassID;
    }

private:
    GrProcessor(const GrProcessor&) = delete;
    GrProcessor& operator=(const GrProcessor&) = delete;

    static uint32_t GenClassID() {
        // fCurrProcessorClassID has been initialized to kIllegalProcessorClassID. The
        // atomic inc returns the old value not the incremented value. So we add
        // 1 to the returned value.
        uint32_t id = static_cast<uint32_t>(sk_atomic_inc(&gCurrProcessorClassID)) + 1;
        if (!id) {
            SK_ABORT("This should never wrap as it should only be called once for each GrProcessor "
                   "subclass.");
        }
        return id;
    }

    enum {
        kIllegalProcessorClassID = 0,
    };
    static int32_t gCurrProcessorClassID;

    uint32_t                                        fClassID;
    RequiredFeatures                                fRequiredFeatures;
};

GR_MAKE_BITFIELD_OPS(GrProcessor::RequiredFeatures);

/** A GrProcessor with the ability to access textures, buffers, and image storages. */
class GrResourceIOProcessor : public GrProcessor {
public:
    class TextureSampler;
    class BufferAccess;
    class ImageStorageAccess;

    int numTextureSamplers() const { return fTextureSamplers.count(); }

    /** Returns the access pattern for the texture at index. index must be valid according to
        numTextureSamplers(). */
    const TextureSampler& textureSampler(int index) const { return *fTextureSamplers[index]; }

    int numBuffers() const { return fBufferAccesses.count(); }

    /** Returns the access pattern for the buffer at index. index must be valid according to
        numBuffers(). */
    const BufferAccess& bufferAccess(int index) const { return *fBufferAccesses[index]; }

    int numImageStorages() const { return fImageStorageAccesses.count(); }

    /** Returns the access object for the image at index. index must be valid according to
        numImages(). */
    const ImageStorageAccess& imageStorageAccess(int index) const {
        return *fImageStorageAccesses[index];
    }

    bool instantiate(GrResourceProvider* resourceProvider) const;

protected:
    GrResourceIOProcessor() {}

    /**
     * Subclasses call these from their constructor to register sampler/image sources. The processor
     * subclass manages the lifetime of the objects (these functions only store pointers). The
     * TextureSampler and/or BufferAccess instances are typically member fields of the GrProcessor
     * subclass. These must only be called from the constructor because GrProcessors are immutable.
     */
    void addTextureSampler(const TextureSampler*);
    void addBufferAccess(const BufferAccess*);
    void addImageStorageAccess(const ImageStorageAccess*);

    bool hasSameSamplersAndAccesses(const GrResourceIOProcessor&) const;

    // These methods can be used by derived classes that also derive from GrProgramElement.
    void addPendingIOs() const;
    void removeRefs() const;
    void pendingIOComplete() const;

private:
    SkSTArray<4, const TextureSampler*, true> fTextureSamplers;
    SkSTArray<1, const BufferAccess*, true> fBufferAccesses;
    SkSTArray<1, const ImageStorageAccess*, true> fImageStorageAccesses;

    typedef GrProcessor INHERITED;
};

/**
 * Used to represent a texture that is required by a GrResourceIOProcessor. It holds a GrTexture
 * along with an associated GrSamplerParams. TextureSamplers don't perform any coord manipulation to
 * account for texture origin.
 */
class GrResourceIOProcessor::TextureSampler {
public:
    /**
     * Must be initialized before adding to a GrProcessor's texture access list.
     */
    TextureSampler();
    /**
     * This copy constructor is used by GrFragmentProcessor::clone() implementations. The copy
     * always takes a new ref on the texture proxy as the new fragment processor will not yet be
     * in pending execution state.
     */
    explicit TextureSampler(const TextureSampler& that)
            : fProxyRef(sk_ref_sp(that.fProxyRef.get()), that.fProxyRef.ioType())
            , fParams(that.fParams)
            , fVisibility(that.fVisibility) {}

    TextureSampler(sk_sp<GrTextureProxy>, const GrSamplerParams&);

    explicit TextureSampler(sk_sp<GrTextureProxy>,
                            GrSamplerParams::FilterMode = GrSamplerParams::kNone_FilterMode,
                            SkShader::TileMode tileXAndY = SkShader::kClamp_TileMode,
                            GrShaderFlags visibility = kFragment_GrShaderFlag);

    TextureSampler& operator=(const TextureSampler&) = delete;

    void reset(sk_sp<GrTextureProxy>, const GrSamplerParams&,
               GrShaderFlags visibility = kFragment_GrShaderFlag);
    void reset(sk_sp<GrTextureProxy>,
               GrSamplerParams::FilterMode = GrSamplerParams::kNone_FilterMode,
               SkShader::TileMode tileXAndY = SkShader::kClamp_TileMode,
               GrShaderFlags visibility = kFragment_GrShaderFlag);

    bool operator==(const TextureSampler& that) const {
        return this->proxy()->underlyingUniqueID() == that.proxy()->underlyingUniqueID() &&
               fParams == that.fParams &&
               fVisibility == that.fVisibility;
    }

    bool operator!=(const TextureSampler& other) const { return !(*this == other); }

    // 'instantiate' should only ever be called at flush time.
    bool instantiate(GrResourceProvider* resourceProvider) const {
        return SkToBool(fProxyRef.get()->instantiate(resourceProvider));
    }

    // 'peekTexture' should only ever be called after a successful 'instantiate' call
    GrTexture* peekTexture() const {
        SkASSERT(fProxyRef.get()->priv().peekTexture());
        return fProxyRef.get()->priv().peekTexture();
    }

    GrTextureProxy* proxy() const { return fProxyRef.get()->asTextureProxy(); }
    GrShaderFlags visibility() const { return fVisibility; }
    const GrSamplerParams& params() const { return fParams; }

    bool isInitialized() const { return SkToBool(fProxyRef.get()); }
    /**
     * For internal use by GrProcessor.
     */
    const GrSurfaceProxyRef* programProxy() const { return &fProxyRef; }

private:
    GrSurfaceProxyRef               fProxyRef;
    GrSamplerParams                 fParams;
    GrShaderFlags                   fVisibility;
};

/**
 * Used to represent a texel buffer that will be read in a GrResourceIOProcessor. It holds a
 * GrBuffer along with an associated offset and texel config.
 */
class GrResourceIOProcessor::BufferAccess {
public:
    BufferAccess() = default;
    BufferAccess(GrPixelConfig texelConfig, GrBuffer* buffer,
                 GrShaderFlags visibility = kFragment_GrShaderFlag) {
        this->reset(texelConfig, buffer, visibility);
    }
    /**
     * This copy constructor is used by GrFragmentProcessor::clone() implementations. The copy
     * always takes a new ref on the buffer proxy as the new fragment processor will not yet be
     * in pending execution state.
     */
    explicit BufferAccess(const BufferAccess& that) {
        this->reset(that.fTexelConfig, that.fBuffer.get(), that.fVisibility);
    }

    BufferAccess& operator=(const BufferAccess&) = delete;

    /**
     * Must be initialized before adding to a GrProcessor's buffer access list.
     */
    void reset(GrPixelConfig texelConfig, GrBuffer* buffer,
               GrShaderFlags visibility = kFragment_GrShaderFlag) {
        fTexelConfig = texelConfig;
        fBuffer.set(SkRef(buffer), kRead_GrIOType);
        fVisibility = visibility;
    }

    bool operator==(const BufferAccess& that) const {
        return fTexelConfig == that.fTexelConfig &&
               this->buffer() == that.buffer() &&
               fVisibility == that.fVisibility;
    }

    bool operator!=(const BufferAccess& that) const { return !(*this == that); }

    GrPixelConfig texelConfig() const { return fTexelConfig; }
    GrBuffer* buffer() const { return fBuffer.get(); }
    GrShaderFlags visibility() const { return fVisibility; }

    /**
     * For internal use by GrProcessor.
     */
    const GrGpuResourceRef* programBuffer() const { return &fBuffer;}

private:
    GrPixelConfig fTexelConfig;
    GrTGpuResourceRef<GrBuffer> fBuffer;
    GrShaderFlags fVisibility;

    typedef SkNoncopyable INHERITED;
};

/**
 * This is used by a GrProcessor to access a texture using image load/store in its shader code.
 * ImageStorageAccesses don't perform any coord manipulation to account for texture origin.
 * Currently the format of the load/store data in the shader is inferred from the texture config,
 * though it could be made explicit.
 */
class GrResourceIOProcessor::ImageStorageAccess {
public:
    ImageStorageAccess(sk_sp<GrTextureProxy>, GrIOType, GrSLMemoryModel, GrSLRestrict,
                       GrShaderFlags visibility = kFragment_GrShaderFlag);
    /**
     * This copy constructor is used by GrFragmentProcessor::clone() implementations. The copy
     * always takes a new ref on the surface proxy as the new fragment processor will not yet be
     * in pending execution state.
     */
    explicit ImageStorageAccess(const ImageStorageAccess& that)
            : fProxyRef(sk_ref_sp(that.fProxyRef.get()), that.fProxyRef.ioType())
            , fVisibility(that.fVisibility)
            , fFormat(that.fFormat)
            , fMemoryModel(that.fMemoryModel)
            , fRestrict(that.fRestrict) {}

    ImageStorageAccess& operator=(const ImageStorageAccess&) = delete;

    bool operator==(const ImageStorageAccess& that) const {
        return this->proxy() == that.proxy() && fVisibility == that.fVisibility;
    }

    bool operator!=(const ImageStorageAccess& that) const { return !(*this == that); }

    GrTextureProxy* proxy() const { return fProxyRef.get()->asTextureProxy(); }
    GrShaderFlags visibility() const { return fVisibility; }
    GrIOType ioType() const { return fProxyRef.ioType(); }
    GrImageStorageFormat format() const { return fFormat; }
    GrSLMemoryModel memoryModel() const { return fMemoryModel; }
    GrSLRestrict restrict() const { return fRestrict; }

    // 'instantiate' should only ever be called at flush time.
    bool instantiate(GrResourceProvider* resourceProvider) const {
        return SkToBool(fProxyRef.get()->instantiate(resourceProvider));
    }
    // 'peekTexture' should only ever be called after a successful 'instantiate' call
    GrTexture* peekTexture() const {
        SkASSERT(fProxyRef.get()->priv().peekTexture());
        return fProxyRef.get()->priv().peekTexture();
    }

    /**
     * For internal use by GrProcessor.
     */
    const GrSurfaceProxyRef* programProxy() const { return &fProxyRef; }

private:
    GrSurfaceProxyRef    fProxyRef;
    GrShaderFlags        fVisibility;
    GrImageStorageFormat fFormat;
    GrSLMemoryModel      fMemoryModel;
    GrSLRestrict         fRestrict;
    typedef SkNoncopyable INHERITED;
};

#endif

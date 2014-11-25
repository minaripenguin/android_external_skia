
/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */



#include "GrDrawTarget.h"
#include "GrContext.h"
#include "GrDrawTargetCaps.h"
#include "GrPath.h"
#include "GrRenderTarget.h"
#include "GrSurfacePriv.h"
#include "GrTemplates.h"
#include "GrTexture.h"
#include "GrVertexBuffer.h"

#include "SkStrokeRec.h"

////////////////////////////////////////////////////////////////////////////////

GrDrawTarget::DrawInfo& GrDrawTarget::DrawInfo::operator =(const DrawInfo& di) {
    fPrimitiveType  = di.fPrimitiveType;
    fStartVertex    = di.fStartVertex;
    fStartIndex     = di.fStartIndex;
    fVertexCount    = di.fVertexCount;
    fIndexCount     = di.fIndexCount;

    fInstanceCount          = di.fInstanceCount;
    fVerticesPerInstance    = di.fVerticesPerInstance;
    fIndicesPerInstance     = di.fIndicesPerInstance;

    if (di.fDevBounds) {
        SkASSERT(di.fDevBounds == &di.fDevBoundsStorage);
        fDevBoundsStorage = di.fDevBoundsStorage;
        fDevBounds = &fDevBoundsStorage;
    } else {
        fDevBounds = NULL;
    }

    this->setVertexBuffer(di.vertexBuffer());
    this->setIndexBuffer(di.indexBuffer());

    return *this;
}

#ifdef SK_DEBUG
bool GrDrawTarget::DrawInfo::isInstanced() const {
    if (fInstanceCount > 0) {
        SkASSERT(0 == fIndexCount % fIndicesPerInstance);
        SkASSERT(0 == fVertexCount % fVerticesPerInstance);
        SkASSERT(fIndexCount / fIndicesPerInstance == fInstanceCount);
        SkASSERT(fVertexCount / fVerticesPerInstance == fInstanceCount);
        // there is no way to specify a non-zero start index to drawIndexedInstances().
        SkASSERT(0 == fStartIndex);
        return true;
    } else {
        SkASSERT(!fVerticesPerInstance);
        SkASSERT(!fIndicesPerInstance);
        return false;
    }
}
#endif

void GrDrawTarget::DrawInfo::adjustInstanceCount(int instanceOffset) {
    SkASSERT(this->isInstanced());
    SkASSERT(instanceOffset + fInstanceCount >= 0);
    fInstanceCount += instanceOffset;
    fVertexCount = fVerticesPerInstance * fInstanceCount;
    fIndexCount = fIndicesPerInstance * fInstanceCount;
}

void GrDrawTarget::DrawInfo::adjustStartVertex(int vertexOffset) {
    fStartVertex += vertexOffset;
    SkASSERT(fStartVertex >= 0);
}

void GrDrawTarget::DrawInfo::adjustStartIndex(int indexOffset) {
    SkASSERT(this->isIndexed());
    fStartIndex += indexOffset;
    SkASSERT(fStartIndex >= 0);
}

////////////////////////////////////////////////////////////////////////////////

#define DEBUG_INVAL_BUFFER 0xdeadcafe
#define DEBUG_INVAL_START_IDX -1

GrDrawTarget::GrDrawTarget(GrContext* context)
    : fClip(NULL)
    , fContext(context)
    , fGpuTraceMarkerCount(0) {
    SkASSERT(context);
    GeometrySrcState& geoSrc = fGeoSrcStateStack.push_back();
#ifdef SK_DEBUG
    geoSrc.fVertexCount = DEBUG_INVAL_START_IDX;
    geoSrc.fVertexBuffer = (GrVertexBuffer*)DEBUG_INVAL_BUFFER;
    geoSrc.fIndexCount = DEBUG_INVAL_START_IDX;
    geoSrc.fIndexBuffer = (GrIndexBuffer*)DEBUG_INVAL_BUFFER;
#endif
    geoSrc.fVertexSrc = kNone_GeometrySrcType;
    geoSrc.fIndexSrc  = kNone_GeometrySrcType;
}

GrDrawTarget::~GrDrawTarget() {
    SkASSERT(1 == fGeoSrcStateStack.count());
    SkDEBUGCODE(GeometrySrcState& geoSrc = fGeoSrcStateStack.back());
    SkASSERT(kNone_GeometrySrcType == geoSrc.fIndexSrc);
    SkASSERT(kNone_GeometrySrcType == geoSrc.fVertexSrc);
}

void GrDrawTarget::releaseGeometry() {
    int popCnt = fGeoSrcStateStack.count() - 1;
    while (popCnt) {
        this->popGeometrySource();
        --popCnt;
    }
    this->resetVertexSource();
    this->resetIndexSource();
}

void GrDrawTarget::setClip(const GrClipData* clip) {
    fClip = clip;
}

const GrClipData* GrDrawTarget::getClip() const {
    return fClip;
}

bool GrDrawTarget::reserveVertexSpace(size_t vertexSize,
                                      int vertexCount,
                                      void** vertices) {
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    bool acquired = false;
    if (vertexCount > 0) {
        SkASSERT(vertices);
        this->releasePreviousVertexSource();
        geoSrc.fVertexSrc = kNone_GeometrySrcType;

        acquired = this->onReserveVertexSpace(vertexSize,
                                              vertexCount,
                                              vertices);
    }
    if (acquired) {
        geoSrc.fVertexSrc = kReserved_GeometrySrcType;
        geoSrc.fVertexCount = vertexCount;
        geoSrc.fVertexSize = vertexSize;
    } else if (vertices) {
        *vertices = NULL;
    }
    return acquired;
}

bool GrDrawTarget::reserveIndexSpace(int indexCount,
                                     void** indices) {
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    bool acquired = false;
    if (indexCount > 0) {
        SkASSERT(indices);
        this->releasePreviousIndexSource();
        geoSrc.fIndexSrc = kNone_GeometrySrcType;

        acquired = this->onReserveIndexSpace(indexCount, indices);
    }
    if (acquired) {
        geoSrc.fIndexSrc = kReserved_GeometrySrcType;
        geoSrc.fIndexCount = indexCount;
    } else if (indices) {
        *indices = NULL;
    }
    return acquired;

}

bool GrDrawTarget::reserveVertexAndIndexSpace(int vertexCount,
                                              size_t vertexStride,
                                              int indexCount,
                                              void** vertices,
                                              void** indices) {
    this->willReserveVertexAndIndexSpace(vertexCount, vertexStride, indexCount);
    if (vertexCount) {
        if (!this->reserveVertexSpace(vertexStride, vertexCount, vertices)) {
            if (indexCount) {
                this->resetIndexSource();
            }
            return false;
        }
    }
    if (indexCount) {
        if (!this->reserveIndexSpace(indexCount, indices)) {
            if (vertexCount) {
                this->resetVertexSource();
            }
            return false;
        }
    }
    return true;
}

bool GrDrawTarget::geometryHints(size_t vertexStride,
                                 int32_t* vertexCount,
                                 int32_t* indexCount) const {
    if (vertexCount) {
        *vertexCount = -1;
    }
    if (indexCount) {
        *indexCount = -1;
    }
    return false;
}

void GrDrawTarget::releasePreviousVertexSource() {
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    switch (geoSrc.fVertexSrc) {
        case kNone_GeometrySrcType:
            break;
        case kReserved_GeometrySrcType:
            this->releaseReservedVertexSpace();
            break;
        case kBuffer_GeometrySrcType:
            geoSrc.fVertexBuffer->unref();
#ifdef SK_DEBUG
            geoSrc.fVertexBuffer = (GrVertexBuffer*)DEBUG_INVAL_BUFFER;
#endif
            break;
        default:
            SkFAIL("Unknown Vertex Source Type.");
            break;
    }
}

void GrDrawTarget::releasePreviousIndexSource() {
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    switch (geoSrc.fIndexSrc) {
        case kNone_GeometrySrcType:   // these two don't require
            break;
        case kReserved_GeometrySrcType:
            this->releaseReservedIndexSpace();
            break;
        case kBuffer_GeometrySrcType:
            geoSrc.fIndexBuffer->unref();
#ifdef SK_DEBUG
            geoSrc.fIndexBuffer = (GrIndexBuffer*)DEBUG_INVAL_BUFFER;
#endif
            break;
        default:
            SkFAIL("Unknown Index Source Type.");
            break;
    }
}

void GrDrawTarget::setVertexSourceToBuffer(const GrVertexBuffer* buffer, size_t vertexStride) {
    this->releasePreviousVertexSource();
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    geoSrc.fVertexSrc    = kBuffer_GeometrySrcType;
    geoSrc.fVertexBuffer = buffer;
    buffer->ref();
    geoSrc.fVertexSize = vertexStride;
}

void GrDrawTarget::setIndexSourceToBuffer(const GrIndexBuffer* buffer) {
    this->releasePreviousIndexSource();
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    geoSrc.fIndexSrc     = kBuffer_GeometrySrcType;
    geoSrc.fIndexBuffer  = buffer;
    buffer->ref();
}

void GrDrawTarget::resetVertexSource() {
    this->releasePreviousVertexSource();
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    geoSrc.fVertexSrc = kNone_GeometrySrcType;
}

void GrDrawTarget::resetIndexSource() {
    this->releasePreviousIndexSource();
    GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    geoSrc.fIndexSrc = kNone_GeometrySrcType;
}

void GrDrawTarget::pushGeometrySource() {
    this->geometrySourceWillPush();
    GeometrySrcState& newState = fGeoSrcStateStack.push_back();
    newState.fIndexSrc = kNone_GeometrySrcType;
    newState.fVertexSrc = kNone_GeometrySrcType;
#ifdef SK_DEBUG
    newState.fVertexCount  = ~0;
    newState.fVertexBuffer = (GrVertexBuffer*)~0;
    newState.fIndexCount   = ~0;
    newState.fIndexBuffer = (GrIndexBuffer*)~0;
#endif
}

void GrDrawTarget::popGeometrySource() {
    // if popping last element then pops are unbalanced with pushes
    SkASSERT(fGeoSrcStateStack.count() > 1);

    this->geometrySourceWillPop(fGeoSrcStateStack.fromBack(1));
    this->releasePreviousVertexSource();
    this->releasePreviousIndexSource();
    fGeoSrcStateStack.pop_back();
}

////////////////////////////////////////////////////////////////////////////////

bool GrDrawTarget::checkDraw(const GrDrawState& drawState,
                             GrPrimitiveType type,
                             int startVertex,
                             int startIndex,
                             int vertexCount,
                             int indexCount) const {
#ifdef SK_DEBUG
    const GeometrySrcState& geoSrc = fGeoSrcStateStack.back();
    int maxVertex = startVertex + vertexCount;
    int maxValidVertex;
    switch (geoSrc.fVertexSrc) {
        case kNone_GeometrySrcType:
            SkFAIL("Attempting to draw without vertex src.");
        case kReserved_GeometrySrcType: // fallthrough
            maxValidVertex = geoSrc.fVertexCount;
            break;
        case kBuffer_GeometrySrcType:
            maxValidVertex = static_cast<int>(geoSrc.fVertexBuffer->gpuMemorySize() / geoSrc.fVertexSize);
            break;
    }
    if (maxVertex > maxValidVertex) {
        SkFAIL("Drawing outside valid vertex range.");
    }
    if (indexCount > 0) {
        int maxIndex = startIndex + indexCount;
        int maxValidIndex;
        switch (geoSrc.fIndexSrc) {
            case kNone_GeometrySrcType:
                SkFAIL("Attempting to draw indexed geom without index src.");
            case kReserved_GeometrySrcType: // fallthrough
                maxValidIndex = geoSrc.fIndexCount;
                break;
            case kBuffer_GeometrySrcType:
                maxValidIndex = static_cast<int>(geoSrc.fIndexBuffer->gpuMemorySize() / sizeof(uint16_t));
                break;
        }
        if (maxIndex > maxValidIndex) {
            SkFAIL("Index reads outside valid index range.");
        }
    }

    SkASSERT(drawState.getRenderTarget());

    if (drawState.hasGeometryProcessor()) {
        const GrGeometryProcessor* gp = drawState.getGeometryProcessor();
        int numTextures = gp->numTextures();
        for (int t = 0; t < numTextures; ++t) {
            GrTexture* texture = gp->texture(t);
            SkASSERT(texture->asRenderTarget() != drawState.getRenderTarget());
        }
    }

    for (int s = 0; s < drawState.numColorStages(); ++s) {
        const GrProcessor* effect = drawState.getColorStage(s).getProcessor();
        int numTextures = effect->numTextures();
        for (int t = 0; t < numTextures; ++t) {
            GrTexture* texture = effect->texture(t);
            SkASSERT(texture->asRenderTarget() != drawState.getRenderTarget());
        }
    }
    for (int s = 0; s < drawState.numCoverageStages(); ++s) {
        const GrProcessor* effect = drawState.getCoverageStage(s).getProcessor();
        int numTextures = effect->numTextures();
        for (int t = 0; t < numTextures; ++t) {
            GrTexture* texture = effect->texture(t);
            SkASSERT(texture->asRenderTarget() != drawState.getRenderTarget());
        }
    }

    SkASSERT(drawState.validateVertexAttribs());
#endif
    if (NULL == drawState.getRenderTarget()) {
        return false;
    }
    return true;
}

bool GrDrawTarget::setupDstReadIfNecessary(GrDrawState* ds,
                                           GrDeviceCoordTexture* dstCopy,
                                           const SkRect* drawBounds) {
    if (this->caps()->dstReadInShaderSupport() || !ds->willEffectReadDstColor()) {
        return true;
    }
    SkIRect copyRect;
    const GrClipData* clip = this->getClip();
    GrRenderTarget* rt = ds->getRenderTarget();
    clip->getConservativeBounds(rt, &copyRect);

    if (drawBounds) {
        SkIRect drawIBounds;
        drawBounds->roundOut(&drawIBounds);
        if (!copyRect.intersect(drawIBounds)) {
#ifdef SK_DEBUG
            SkDebugf("Missed an early reject. Bailing on draw from setupDstReadIfNecessary.\n");
#endif
            return false;
        }
    } else {
#ifdef SK_DEBUG
        //SkDebugf("No dev bounds when dst copy is made.\n");
#endif
    }

    // MSAA consideration: When there is support for reading MSAA samples in the shader we could
    // have per-sample dst values by making the copy multisampled.
    GrSurfaceDesc desc;
    this->initCopySurfaceDstDesc(rt, &desc);
    desc.fWidth = copyRect.width();
    desc.fHeight = copyRect.height();

    SkAutoTUnref<GrTexture> copy(
        fContext->refScratchTexture(desc, GrContext::kApprox_ScratchTexMatch));

    if (!copy) {
        SkDebugf("Failed to create temporary copy of destination texture.\n");
        return false;
    }
    SkIPoint dstPoint = {0, 0};
    if (this->copySurface(copy, rt, copyRect, dstPoint)) {
        dstCopy->setTexture(copy);
        dstCopy->setOffset(copyRect.fLeft, copyRect.fTop);
        return true;
    } else {
        return false;
    }
}

void GrDrawTarget::drawIndexed(GrDrawState* ds,
                               GrPrimitiveType type,
                               int startVertex,
                               int startIndex,
                               int vertexCount,
                               int indexCount,
                               const SkRect* devBounds) {
    SkASSERT(ds);
    if (indexCount > 0 &&
        this->checkDraw(*ds, type, startVertex, startIndex, vertexCount, indexCount)) {

        // Setup clip
        GrClipMaskManager::ScissorState scissorState;
        GrDrawState::AutoRestoreEffects are;
        GrDrawState::AutoRestoreStencil ars;
        if (!this->setupClip(devBounds, &are, &ars, ds, &scissorState)) {
            return;
        }

        DrawInfo info;
        info.fPrimitiveType = type;
        info.fStartVertex   = startVertex;
        info.fStartIndex    = startIndex;
        info.fVertexCount   = vertexCount;
        info.fIndexCount    = indexCount;

        info.fInstanceCount         = 0;
        info.fVerticesPerInstance   = 0;
        info.fIndicesPerInstance    = 0;

        if (devBounds) {
            info.setDevBounds(*devBounds);
        }

        // TODO: We should continue with incorrect blending.
        GrDeviceCoordTexture dstCopy;
        if (!this->setupDstReadIfNecessary(ds, &dstCopy, devBounds)) {
            return;
        }
        this->setDrawBuffers(&info);

        this->onDraw(*ds, info, scissorState, dstCopy.texture() ? &dstCopy : NULL);
    }
}

void GrDrawTarget::drawNonIndexed(GrDrawState* ds,
                                  GrPrimitiveType type,
                                  int startVertex,
                                  int vertexCount,
                                  const SkRect* devBounds) {
    SkASSERT(ds);
    if (vertexCount > 0 && this->checkDraw(*ds, type, startVertex, -1, vertexCount, -1)) {

        // Setup clip
        GrClipMaskManager::ScissorState scissorState;
        GrDrawState::AutoRestoreEffects are;
        GrDrawState::AutoRestoreStencil ars;
        if (!this->setupClip(devBounds, &are, &ars, ds, &scissorState)) {
            return;
        }

        DrawInfo info;
        info.fPrimitiveType = type;
        info.fStartVertex   = startVertex;
        info.fStartIndex    = 0;
        info.fVertexCount   = vertexCount;
        info.fIndexCount    = 0;

        info.fInstanceCount         = 0;
        info.fVerticesPerInstance   = 0;
        info.fIndicesPerInstance    = 0;

        if (devBounds) {
            info.setDevBounds(*devBounds);
        }

        // TODO: We should continue with incorrect blending.
        GrDeviceCoordTexture dstCopy;
        if (!this->setupDstReadIfNecessary(ds, &dstCopy, devBounds)) {
            return;
        }

        this->setDrawBuffers(&info);

        this->onDraw(*ds, info, scissorState, dstCopy.texture() ? &dstCopy : NULL);
    }
}

static const GrStencilSettings& winding_path_stencil_settings() {
    GR_STATIC_CONST_SAME_STENCIL_STRUCT(gSettings,
        kIncClamp_StencilOp,
        kIncClamp_StencilOp,
        kAlwaysIfInClip_StencilFunc,
        0xFFFF, 0xFFFF, 0xFFFF);
    return *GR_CONST_STENCIL_SETTINGS_PTR_FROM_STRUCT_PTR(&gSettings);
}

static const GrStencilSettings& even_odd_path_stencil_settings() {
    GR_STATIC_CONST_SAME_STENCIL_STRUCT(gSettings,
        kInvert_StencilOp,
        kInvert_StencilOp,
        kAlwaysIfInClip_StencilFunc,
        0xFFFF, 0xFFFF, 0xFFFF);
    return *GR_CONST_STENCIL_SETTINGS_PTR_FROM_STRUCT_PTR(&gSettings);
}

void GrDrawTarget::getPathStencilSettingsForFilltype(GrPathRendering::FillType fill,
                                                     const GrStencilBuffer* sb,
                                                     GrStencilSettings* outStencilSettings) {

    switch (fill) {
        default:
            SkFAIL("Unexpected path fill.");
        case GrPathRendering::kWinding_FillType:
            *outStencilSettings = winding_path_stencil_settings();
            break;
        case GrPathRendering::kEvenOdd_FillType:
            *outStencilSettings = even_odd_path_stencil_settings();
            break;
    }
    this->clipMaskManager()->adjustPathStencilParams(sb, outStencilSettings);
}

void GrDrawTarget::stencilPath(GrDrawState* ds,
                               const GrPath* path,
                               GrPathRendering::FillType fill) {
    // TODO: extract portions of checkDraw that are relevant to path stenciling.
    SkASSERT(path);
    SkASSERT(this->caps()->pathRenderingSupport());
    SkASSERT(ds);

    // Setup clip
    GrClipMaskManager::ScissorState scissorState;
    GrDrawState::AutoRestoreEffects are;
    GrDrawState::AutoRestoreStencil ars;
    if (!this->setupClip(NULL, &are, &ars, ds, &scissorState)) {
        return;
    }

    // set stencil settings for path
    GrStencilSettings stencilSettings;
    this->getPathStencilSettingsForFilltype(fill,
                                            ds->getRenderTarget()->getStencilBuffer(),
                                            &stencilSettings);

    this->onStencilPath(*ds, path, scissorState, stencilSettings);
}

void GrDrawTarget::drawPath(GrDrawState* ds,
                            const GrPath* path,
                            GrPathRendering::FillType fill) {
    // TODO: extract portions of checkDraw that are relevant to path rendering.
    SkASSERT(path);
    SkASSERT(this->caps()->pathRenderingSupport());
    SkASSERT(ds);

    SkRect devBounds = path->getBounds();
    SkMatrix viewM = ds->getViewMatrix();
    viewM.mapRect(&devBounds);

    // Setup clip
    GrClipMaskManager::ScissorState scissorState;
    GrDrawState::AutoRestoreEffects are;
    GrDrawState::AutoRestoreStencil ars;
    if (!this->setupClip(&devBounds, &are, &ars, ds, &scissorState)) {
       return;
    }

    // set stencil settings for path
    GrStencilSettings stencilSettings;
    this->getPathStencilSettingsForFilltype(fill,
                                            ds->getRenderTarget()->getStencilBuffer(),
                                            &stencilSettings);

    GrDeviceCoordTexture dstCopy;
    if (!this->setupDstReadIfNecessary(ds, &dstCopy, &devBounds)) {
        return;
    }

    this->onDrawPath(*ds, path, scissorState, stencilSettings, dstCopy.texture() ? &dstCopy : NULL);
}

void GrDrawTarget::drawPaths(GrDrawState* ds,
                             const GrPathRange* pathRange,
                             const uint32_t indices[],
                             int count,
                             const float transforms[],
                             PathTransformType transformsType,
                             GrPathRendering::FillType fill) {
    SkASSERT(this->caps()->pathRenderingSupport());
    SkASSERT(pathRange);
    SkASSERT(indices);
    SkASSERT(transforms);
    SkASSERT(ds);

    // Setup clip
    GrClipMaskManager::ScissorState scissorState;
    GrDrawState::AutoRestoreEffects are;
    GrDrawState::AutoRestoreStencil ars;

    if (!this->setupClip(NULL, &are, &ars, ds, &scissorState)) {
        return;
    }

    // set stencil settings for path
    GrStencilSettings stencilSettings;
    this->getPathStencilSettingsForFilltype(fill,
                                            ds->getRenderTarget()->getStencilBuffer(),
                                            &stencilSettings);

    // Don't compute a bounding box for setupDstReadIfNecessary(), we'll opt
    // instead for it to just copy the entire dst. Realistically this is a moot
    // point, because any context that supports NV_path_rendering will also
    // support NV_blend_equation_advanced.
    GrDeviceCoordTexture dstCopy;
    if (!this->setupDstReadIfNecessary(ds, &dstCopy, NULL)) {
        return;
    }

    this->onDrawPaths(*ds, pathRange, indices, count, transforms, transformsType, scissorState,
                      stencilSettings, dstCopy.texture() ? &dstCopy : NULL);
}

void GrDrawTarget::clear(const SkIRect* rect,
                         GrColor color,
                         bool canIgnoreRect,
                         GrRenderTarget* renderTarget) {
    if (fCaps->useDrawInsteadOfClear()) {
        // This works around a driver bug with clear by drawing a rect instead.
        // The driver will ignore a clear if it is the only thing rendered to a
        // target before the target is read.
        SkIRect rtRect = SkIRect::MakeWH(renderTarget->width(), renderTarget->height());
        if (NULL == rect || canIgnoreRect || rect->contains(rtRect)) {
            rect = &rtRect;
            // We first issue a discard() since that may help tilers.
            this->discard(renderTarget);
        }

        GrDrawState drawState;

        drawState.setColor(color);
        drawState.setRenderTarget(renderTarget);

        this->drawSimpleRect(&drawState, *rect);
    } else {       
        this->onClear(rect, color, canIgnoreRect, renderTarget);
    }
}

typedef GrTraceMarkerSet::Iter TMIter;
void GrDrawTarget::saveActiveTraceMarkers() {
    if (this->caps()->gpuTracingSupport()) {
        SkASSERT(0 == fStoredTraceMarkers.count());
        fStoredTraceMarkers.addSet(fActiveTraceMarkers);
        for (TMIter iter = fStoredTraceMarkers.begin(); iter != fStoredTraceMarkers.end(); ++iter) {
            this->removeGpuTraceMarker(&(*iter));
        }
    }
}

void GrDrawTarget::restoreActiveTraceMarkers() {
    if (this->caps()->gpuTracingSupport()) {
        SkASSERT(0 == fActiveTraceMarkers.count());
        for (TMIter iter = fStoredTraceMarkers.begin(); iter != fStoredTraceMarkers.end(); ++iter) {
            this->addGpuTraceMarker(&(*iter));
        }
        for (TMIter iter = fActiveTraceMarkers.begin(); iter != fActiveTraceMarkers.end(); ++iter) {
            this->fStoredTraceMarkers.remove(*iter);
        }
    }
}

void GrDrawTarget::addGpuTraceMarker(const GrGpuTraceMarker* marker) {
    if (this->caps()->gpuTracingSupport()) {
        SkASSERT(fGpuTraceMarkerCount >= 0);
        this->fActiveTraceMarkers.add(*marker);
        ++fGpuTraceMarkerCount;
    }
}

void GrDrawTarget::removeGpuTraceMarker(const GrGpuTraceMarker* marker) {
    if (this->caps()->gpuTracingSupport()) {
        SkASSERT(fGpuTraceMarkerCount >= 1);
        this->fActiveTraceMarkers.remove(*marker);
        --fGpuTraceMarkerCount;
    }
}

////////////////////////////////////////////////////////////////////////////////

void GrDrawTarget::drawIndexedInstances(GrDrawState* ds,
                                        GrPrimitiveType type,
                                        int instanceCount,
                                        int verticesPerInstance,
                                        int indicesPerInstance,
                                        const SkRect* devBounds) {
    SkASSERT(ds);

    if (!verticesPerInstance || !indicesPerInstance) {
        return;
    }

    int maxInstancesPerDraw = this->indexCountInCurrentSource() / indicesPerInstance;
    if (!maxInstancesPerDraw) {
        return;
    }

    // Setup clip
    GrClipMaskManager::ScissorState scissorState;
    GrDrawState::AutoRestoreEffects are;
    GrDrawState::AutoRestoreStencil ars;
    if (!this->setupClip(devBounds, &are, &ars, ds, &scissorState)) {
        return;
    }

    DrawInfo info;
    info.fPrimitiveType = type;
    info.fStartIndex = 0;
    info.fStartVertex = 0;
    info.fIndicesPerInstance = indicesPerInstance;
    info.fVerticesPerInstance = verticesPerInstance;

    // Set the same bounds for all the draws.
    if (devBounds) {
        info.setDevBounds(*devBounds);
    }

    // TODO: We should continue with incorrect blending.
    GrDeviceCoordTexture dstCopy;
    if (!this->setupDstReadIfNecessary(ds, &dstCopy, devBounds)) {
        return;
    }

    while (instanceCount) {
        info.fInstanceCount = SkTMin(instanceCount, maxInstancesPerDraw);
        info.fVertexCount = info.fInstanceCount * verticesPerInstance;
        info.fIndexCount = info.fInstanceCount * indicesPerInstance;

        this->setDrawBuffers(&info);

        if (this->checkDraw(*ds,
                            type,
                            info.fStartVertex,
                            info.fStartIndex,
                            info.fVertexCount,
                            info.fIndexCount)) {
            this->onDraw(*ds, info, scissorState, dstCopy.texture() ? &dstCopy : NULL);
        }
        info.fStartVertex += info.fVertexCount;
        instanceCount -= info.fInstanceCount;
    }
}

////////////////////////////////////////////////////////////////////////////////

GrDrawTarget::AutoReleaseGeometry::AutoReleaseGeometry(
                                         GrDrawTarget*  target,
                                         int vertexCount,
                                         size_t vertexStride,
                                         int indexCount) {
    fTarget = NULL;
    this->set(target, vertexCount, vertexStride, indexCount);
}

GrDrawTarget::AutoReleaseGeometry::AutoReleaseGeometry() {
    fTarget = NULL;
}

GrDrawTarget::AutoReleaseGeometry::~AutoReleaseGeometry() {
    this->reset();
}

bool GrDrawTarget::AutoReleaseGeometry::set(GrDrawTarget*  target,
                                            int vertexCount,
                                            size_t vertexStride,
                                            int indexCount) {
    this->reset();
    fTarget = target;
    bool success = true;
    if (fTarget) {
        success = target->reserveVertexAndIndexSpace(vertexCount,
                                                     vertexStride,
                                                     indexCount,
                                                     &fVertices,
                                                     &fIndices);
        if (!success) {
            fTarget = NULL;
            this->reset();
        }
    }
    SkASSERT(success == SkToBool(fTarget));
    return success;
}

void GrDrawTarget::AutoReleaseGeometry::reset() {
    if (fTarget) {
        if (fVertices) {
            fTarget->resetVertexSource();
        }
        if (fIndices) {
            fTarget->resetIndexSource();
        }
        fTarget = NULL;
    }
    fVertices = NULL;
    fIndices = NULL;
}

GrDrawTarget::AutoClipRestore::AutoClipRestore(GrDrawTarget* target, const SkIRect& newClip) {
    fTarget = target;
    fClip = fTarget->getClip();
    fStack.init();
    fStack.get()->clipDevRect(newClip, SkRegion::kReplace_Op);
    fReplacementClip.fClipStack = fStack.get();
    target->setClip(&fReplacementClip);
}

namespace {
// returns true if the read/written rect intersects the src/dst and false if not.
bool clip_srcrect_and_dstpoint(const GrSurface* dst,
                               const GrSurface* src,
                               const SkIRect& srcRect,
                               const SkIPoint& dstPoint,
                               SkIRect* clippedSrcRect,
                               SkIPoint* clippedDstPoint) {
    *clippedSrcRect = srcRect;
    *clippedDstPoint = dstPoint;

    // clip the left edge to src and dst bounds, adjusting dstPoint if necessary
    if (clippedSrcRect->fLeft < 0) {
        clippedDstPoint->fX -= clippedSrcRect->fLeft;
        clippedSrcRect->fLeft = 0;
    }
    if (clippedDstPoint->fX < 0) {
        clippedSrcRect->fLeft -= clippedDstPoint->fX;
        clippedDstPoint->fX = 0;
    }

    // clip the top edge to src and dst bounds, adjusting dstPoint if necessary
    if (clippedSrcRect->fTop < 0) {
        clippedDstPoint->fY -= clippedSrcRect->fTop;
        clippedSrcRect->fTop = 0;
    }
    if (clippedDstPoint->fY < 0) {
        clippedSrcRect->fTop -= clippedDstPoint->fY;
        clippedDstPoint->fY = 0;
    }

    // clip the right edge to the src and dst bounds.
    if (clippedSrcRect->fRight > src->width()) {
        clippedSrcRect->fRight = src->width();
    }
    if (clippedDstPoint->fX + clippedSrcRect->width() > dst->width()) {
        clippedSrcRect->fRight = clippedSrcRect->fLeft + dst->width() - clippedDstPoint->fX;
    }

    // clip the bottom edge to the src and dst bounds.
    if (clippedSrcRect->fBottom > src->height()) {
        clippedSrcRect->fBottom = src->height();
    }
    if (clippedDstPoint->fY + clippedSrcRect->height() > dst->height()) {
        clippedSrcRect->fBottom = clippedSrcRect->fTop + dst->height() - clippedDstPoint->fY;
    }

    // The above clipping steps may have inverted the rect if it didn't intersect either the src or
    // dst bounds.
    return !clippedSrcRect->isEmpty();
}
}

bool GrDrawTarget::copySurface(GrSurface* dst,
                               GrSurface* src,
                               const SkIRect& srcRect,
                               const SkIPoint& dstPoint) {
    SkASSERT(dst);
    SkASSERT(src);

    SkIRect clippedSrcRect;
    SkIPoint clippedDstPoint;
    // If the rect is outside the src or dst then we've already succeeded.
    if (!clip_srcrect_and_dstpoint(dst,
                                   src,
                                   srcRect,
                                   dstPoint,
                                   &clippedSrcRect,
                                   &clippedDstPoint)) {
        SkASSERT(GrDrawTarget::canCopySurface(dst, src, srcRect, dstPoint));
        return true;
    }

    if (!GrDrawTarget::canCopySurface(dst, src, clippedSrcRect, clippedDstPoint)) {
        return false;
    }

    GrRenderTarget* rt = dst->asRenderTarget();
    GrTexture* tex = src->asTexture();

    GrDrawState drawState;
    drawState.setRenderTarget(rt);
    SkMatrix matrix;
    matrix.setTranslate(SkIntToScalar(clippedSrcRect.fLeft - clippedDstPoint.fX),
                        SkIntToScalar(clippedSrcRect.fTop - clippedDstPoint.fY));
    matrix.postIDiv(tex->width(), tex->height());
    drawState.addColorTextureProcessor(tex, matrix);
    SkIRect dstRect = SkIRect::MakeXYWH(clippedDstPoint.fX,
                                        clippedDstPoint.fY,
                                        clippedSrcRect.width(),
                                        clippedSrcRect.height());
    this->drawSimpleRect(&drawState, dstRect);
    return true;
}

bool GrDrawTarget::canCopySurface(const GrSurface* dst,
                                  const GrSurface* src,
                                  const SkIRect& srcRect,
                                  const SkIPoint& dstPoint) {
    SkASSERT(dst);
    SkASSERT(src);

    SkIRect clippedSrcRect;
    SkIPoint clippedDstPoint;
    // If the rect is outside the src or dst then we're guaranteed success
    if (!clip_srcrect_and_dstpoint(dst,
                                   src,
                                   srcRect,
                                   dstPoint,
                                   &clippedSrcRect,
                                   &clippedDstPoint)) {
        return true;
    }

    // Check that the read/write rects are contained within the src/dst bounds.
    SkASSERT(!clippedSrcRect.isEmpty());
    SkASSERT(SkIRect::MakeWH(src->width(), src->height()).contains(clippedSrcRect));
    SkASSERT(clippedDstPoint.fX >= 0 && clippedDstPoint.fY >= 0);
    SkASSERT(clippedDstPoint.fX + clippedSrcRect.width() <= dst->width() &&
             clippedDstPoint.fY + clippedSrcRect.height() <= dst->height());

    return (dst != src) && dst->asRenderTarget() && src->asTexture();
}

void GrDrawTarget::initCopySurfaceDstDesc(const GrSurface* src, GrSurfaceDesc* desc) {
    // Make the dst of the copy be a render target because the default copySurface draws to the dst.
    desc->fOrigin = kDefault_GrSurfaceOrigin;
    desc->fFlags = kRenderTarget_GrSurfaceFlag | kNoStencil_GrSurfaceFlag;
    desc->fConfig = src->config();
}

///////////////////////////////////////////////////////////////////////////////

void GrDrawTargetCaps::reset() {
    fMipMapSupport = false;
    fNPOTTextureTileSupport = false;
    fTwoSidedStencilSupport = false;
    fStencilWrapOpsSupport = false;
    fHWAALineSupport = false;
    fShaderDerivativeSupport = false;
    fGeometryShaderSupport = false;
    fDualSourceBlendingSupport = false;
    fPathRenderingSupport = false;
    fDstReadInShaderSupport = false;
    fDiscardRenderTargetSupport = false;
    fReuseScratchTextures = true;
    fGpuTracingSupport = false;
    fCompressedTexSubImageSupport = false;

    fUseDrawInsteadOfClear = false;

    fMapBufferFlags = kNone_MapFlags;

    fMaxRenderTargetSize = 0;
    fMaxTextureSize = 0;
    fMaxSampleCount = 0;

    memset(fConfigRenderSupport, 0, sizeof(fConfigRenderSupport));
    memset(fConfigTextureSupport, 0, sizeof(fConfigTextureSupport));
}

GrDrawTargetCaps& GrDrawTargetCaps::operator=(const GrDrawTargetCaps& other) {
    fMipMapSupport = other.fMipMapSupport;
    fNPOTTextureTileSupport = other.fNPOTTextureTileSupport;
    fTwoSidedStencilSupport = other.fTwoSidedStencilSupport;
    fStencilWrapOpsSupport = other.fStencilWrapOpsSupport;
    fHWAALineSupport = other.fHWAALineSupport;
    fShaderDerivativeSupport = other.fShaderDerivativeSupport;
    fGeometryShaderSupport = other.fGeometryShaderSupport;
    fDualSourceBlendingSupport = other.fDualSourceBlendingSupport;
    fPathRenderingSupport = other.fPathRenderingSupport;
    fDstReadInShaderSupport = other.fDstReadInShaderSupport;
    fDiscardRenderTargetSupport = other.fDiscardRenderTargetSupport;
    fReuseScratchTextures = other.fReuseScratchTextures;
    fGpuTracingSupport = other.fGpuTracingSupport;
    fCompressedTexSubImageSupport = other.fCompressedTexSubImageSupport;

    fUseDrawInsteadOfClear = other.fUseDrawInsteadOfClear;

    fMapBufferFlags = other.fMapBufferFlags;

    fMaxRenderTargetSize = other.fMaxRenderTargetSize;
    fMaxTextureSize = other.fMaxTextureSize;
    fMaxSampleCount = other.fMaxSampleCount;

    memcpy(fConfigRenderSupport, other.fConfigRenderSupport, sizeof(fConfigRenderSupport));
    memcpy(fConfigTextureSupport, other.fConfigTextureSupport, sizeof(fConfigTextureSupport));

    return *this;
}

static SkString map_flags_to_string(uint32_t flags) {
    SkString str;
    if (GrDrawTargetCaps::kNone_MapFlags == flags) {
        str = "none";
    } else {
        SkASSERT(GrDrawTargetCaps::kCanMap_MapFlag & flags);
        SkDEBUGCODE(flags &= ~GrDrawTargetCaps::kCanMap_MapFlag);
        str = "can_map";

        if (GrDrawTargetCaps::kSubset_MapFlag & flags) {
            str.append(" partial");
        } else {
            str.append(" full");
        }
        SkDEBUGCODE(flags &= ~GrDrawTargetCaps::kSubset_MapFlag);
    }
    SkASSERT(0 == flags); // Make sure we handled all the flags.
    return str;
}

SkString GrDrawTargetCaps::dump() const {
    SkString r;
    static const char* gNY[] = {"NO", "YES"};
    r.appendf("MIP Map Support                    : %s\n", gNY[fMipMapSupport]);
    r.appendf("NPOT Texture Tile Support          : %s\n", gNY[fNPOTTextureTileSupport]);
    r.appendf("Two Sided Stencil Support          : %s\n", gNY[fTwoSidedStencilSupport]);
    r.appendf("Stencil Wrap Ops  Support          : %s\n", gNY[fStencilWrapOpsSupport]);
    r.appendf("HW AA Lines Support                : %s\n", gNY[fHWAALineSupport]);
    r.appendf("Shader Derivative Support          : %s\n", gNY[fShaderDerivativeSupport]);
    r.appendf("Geometry Shader Support            : %s\n", gNY[fGeometryShaderSupport]);
    r.appendf("Dual Source Blending Support       : %s\n", gNY[fDualSourceBlendingSupport]);
    r.appendf("Path Rendering Support             : %s\n", gNY[fPathRenderingSupport]);
    r.appendf("Dst Read In Shader Support         : %s\n", gNY[fDstReadInShaderSupport]);
    r.appendf("Discard Render Target Support      : %s\n", gNY[fDiscardRenderTargetSupport]);
    r.appendf("Reuse Scratch Textures             : %s\n", gNY[fReuseScratchTextures]);
    r.appendf("Gpu Tracing Support                : %s\n", gNY[fGpuTracingSupport]);
    r.appendf("Compressed Update Support          : %s\n", gNY[fCompressedTexSubImageSupport]);

    r.appendf("Draw Instead of Clear [workaround] : %s\n", gNY[fUseDrawInsteadOfClear]);

    r.appendf("Max Texture Size                   : %d\n", fMaxTextureSize);
    r.appendf("Max Render Target Size             : %d\n", fMaxRenderTargetSize);
    r.appendf("Max Sample Count                   : %d\n", fMaxSampleCount);

    r.appendf("Map Buffer Support                 : %s\n",
              map_flags_to_string(fMapBufferFlags).c_str());

    static const char* kConfigNames[] = {
        "Unknown",  // kUnknown_GrPixelConfig
        "Alpha8",   // kAlpha_8_GrPixelConfig,
        "Index8",   // kIndex_8_GrPixelConfig,
        "RGB565",   // kRGB_565_GrPixelConfig,
        "RGBA444",  // kRGBA_4444_GrPixelConfig,
        "RGBA8888", // kRGBA_8888_GrPixelConfig,
        "BGRA8888", // kBGRA_8888_GrPixelConfig,
        "ETC1",     // kETC1_GrPixelConfig,
        "LATC",     // kLATC_GrPixelConfig,
        "R11EAC",   // kR11_EAC_GrPixelConfig,
        "ASTC12x12",// kASTC_12x12_GrPixelConfig,
        "RGBAFloat",  // kRGBA_float_GrPixelConfig
    };
    GR_STATIC_ASSERT(0  == kUnknown_GrPixelConfig);
    GR_STATIC_ASSERT(1  == kAlpha_8_GrPixelConfig);
    GR_STATIC_ASSERT(2  == kIndex_8_GrPixelConfig);
    GR_STATIC_ASSERT(3  == kRGB_565_GrPixelConfig);
    GR_STATIC_ASSERT(4  == kRGBA_4444_GrPixelConfig);
    GR_STATIC_ASSERT(5  == kRGBA_8888_GrPixelConfig);
    GR_STATIC_ASSERT(6  == kBGRA_8888_GrPixelConfig);
    GR_STATIC_ASSERT(7  == kETC1_GrPixelConfig);
    GR_STATIC_ASSERT(8  == kLATC_GrPixelConfig);
    GR_STATIC_ASSERT(9  == kR11_EAC_GrPixelConfig);
    GR_STATIC_ASSERT(10 == kASTC_12x12_GrPixelConfig);
    GR_STATIC_ASSERT(11 == kRGBA_float_GrPixelConfig);
    GR_STATIC_ASSERT(SK_ARRAY_COUNT(kConfigNames) == kGrPixelConfigCnt);

    SkASSERT(!fConfigRenderSupport[kUnknown_GrPixelConfig][0]);
    SkASSERT(!fConfigRenderSupport[kUnknown_GrPixelConfig][1]);

    for (size_t i = 1; i < SK_ARRAY_COUNT(kConfigNames); ++i)  {
        r.appendf("%s is renderable: %s, with MSAA: %s\n",
                  kConfigNames[i],
                  gNY[fConfigRenderSupport[i][0]],
                  gNY[fConfigRenderSupport[i][1]]);
    }

    SkASSERT(!fConfigTextureSupport[kUnknown_GrPixelConfig]);

    for (size_t i = 1; i < SK_ARRAY_COUNT(kConfigNames); ++i)  {
        r.appendf("%s is uploadable to a texture: %s\n",
                  kConfigNames[i],
                  gNY[fConfigTextureSupport[i]]);
    }

    return r;
}

uint32_t GrDrawTargetCaps::CreateUniqueID() {
    static int32_t gUniqueID = SK_InvalidUniqueID;
    uint32_t id;
    do {
        id = static_cast<uint32_t>(sk_atomic_inc(&gUniqueID) + 1);
    } while (id == SK_InvalidUniqueID);
    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool GrClipTarget::setupClip(const SkRect* devBounds,
                             GrDrawState::AutoRestoreEffects* are,
                             GrDrawState::AutoRestoreStencil* ars,
                             GrDrawState* ds,
                             GrClipMaskManager::ScissorState* scissorState) {
    return fClipMaskManager.setupClipping(ds,
                                          are,
                                          ars,
                                          scissorState,
                                          this->getClip(),
                                          devBounds);
}

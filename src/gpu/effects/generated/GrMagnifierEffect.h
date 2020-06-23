/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrMagnifierEffect.fp; do not modify.
 **************************************************************************************************/
#ifndef GrMagnifierEffect_DEFINED
#define GrMagnifierEffect_DEFINED

#include "include/core/SkM44.h"
#include "include/core/SkTypes.h"

#include "src/gpu/GrCoordTransform.h"
#include "src/gpu/GrFragmentProcessor.h"

class GrMagnifierEffect : public GrFragmentProcessor {
public:
    static std::unique_ptr<GrFragmentProcessor> Make(std::unique_ptr<GrFragmentProcessor> src,
                                                     SkIRect bounds,
                                                     SkRect srcRect,
                                                     float xInvZoom,
                                                     float yInvZoom,
                                                     float xInvInset,
                                                     float yInvInset) {
        return std::unique_ptr<GrFragmentProcessor>(new GrMagnifierEffect(
                std::move(src), bounds, srcRect, xInvZoom, yInvZoom, xInvInset, yInvInset));
    }
    GrMagnifierEffect(const GrMagnifierEffect& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "MagnifierEffect"; }
    GrCoordTransform fCoordTransform0;
    int src_index = -1;
    SkIRect bounds;
    SkRect srcRect;
    float xInvZoom;
    float yInvZoom;
    float xInvInset;
    float yInvInset;

private:
    GrMagnifierEffect(std::unique_ptr<GrFragmentProcessor> src,
                      SkIRect bounds,
                      SkRect srcRect,
                      float xInvZoom,
                      float yInvZoom,
                      float xInvInset,
                      float yInvInset)
            : INHERITED(kGrMagnifierEffect_ClassID, kNone_OptimizationFlags)
            , fCoordTransform0(SkMatrix::I())
            , bounds(bounds)
            , srcRect(srcRect)
            , xInvZoom(xInvZoom)
            , yInvZoom(yInvZoom)
            , xInvInset(xInvInset)
            , yInvInset(yInvInset) {
        SkASSERT(src);
        src_index = this->registerExplicitlySampledChild(std::move(src));
        this->addCoordTransform(&fCoordTransform0);
    }
    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    typedef GrFragmentProcessor INHERITED;
};
#endif

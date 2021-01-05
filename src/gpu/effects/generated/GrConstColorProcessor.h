/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrConstColorProcessor.fp; do not modify.
 **************************************************************************************************/
#ifndef GrConstColorProcessor_DEFINED
#define GrConstColorProcessor_DEFINED

#include "include/core/SkM44.h"
#include "include/core/SkTypes.h"

#include "src/gpu/GrFragmentProcessor.h"

class GrConstColorProcessor : public GrFragmentProcessor {
public:
    SkPMColor4f constantOutputForConstantInput(const SkPMColor4f& inColor) const override {
        return color;
    }
    static std::unique_ptr<GrFragmentProcessor> Make(SkPMColor4f color) {
        return std::unique_ptr<GrFragmentProcessor>(new GrConstColorProcessor(color));
    }
    GrConstColorProcessor(const GrConstColorProcessor& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "ConstColorProcessor"; }
    SkPMColor4f color;

private:
    GrConstColorProcessor(SkPMColor4f color)
            : INHERITED(
                      kGrConstColorProcessor_ClassID,
                      (OptimizationFlags)(kConstantOutputForConstantInput_OptimizationFlag |
                                          (color.isOpaque() ? kPreservesOpaqueInput_OptimizationFlag
                                                            : kNone_OptimizationFlags)))
            , color(color) {}
    GrGLSLFragmentProcessor* onCreateGLSLInstance() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
#if GR_TEST_UTILS
    SkString onDumpInfo() const override;
#endif
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    using INHERITED = GrFragmentProcessor;
};
#endif

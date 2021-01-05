

/**************************************************************************************************
 *** This file was autogenerated from GrFunction.fp; do not modify.
 **************************************************************************************************/
#include "GrFunction.h"

#include "src/core/SkUtils.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"
#include "src/gpu/glsl/GrGLSLProgramBuilder.h"
#include "src/sksl/SkSLCPP.h"
#include "src/sksl/SkSLUtil.h"
class GrGLSLFunction : public GrGLSLFragmentProcessor {
public:
    GrGLSLFunction() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrFunction& _outer = args.fFp.cast<GrFunction>();
        (void) _outer;
        colorVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag, kHalf4_GrSLType, "color");
        SkString flip_name = fragBuilder->getMangledFunctionName("flip");
        const GrShaderVar flip_args[] = { GrShaderVar("c", kHalf4_GrSLType) };
        fragBuilder->emitFunction(kHalf4_GrSLType, flip_name.c_str(), {flip_args, 1},
R"SkSL(int x = 42;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
++x;
return c.wzyx;
)SkSL");
        fragBuilder->codeAppendf(
R"SkSL(return %s(%s(%s(%s)));
)SkSL"
, flip_name.c_str(), flip_name.c_str(), flip_name.c_str(), args.fUniformHandler->getUniformCStr(colorVar));
    }
private:
    void onSetData(const GrGLSLProgramDataManager& pdman, const GrFragmentProcessor& _proc) override {
    }
    UniformHandle colorVar;
};
GrGLSLFragmentProcessor* GrFunction::onCreateGLSLInstance() const {
    return new GrGLSLFunction();
}
void GrFunction::onGetGLSLProcessorKey(const GrShaderCaps& caps, GrProcessorKeyBuilder* b) const {
}
bool GrFunction::onIsEqual(const GrFragmentProcessor& other) const {
    const GrFunction& that = other.cast<GrFunction>();
    (void) that;
    return true;
}
GrFunction::GrFunction(const GrFunction& src)
: INHERITED(kGrFunction_ClassID, src.optimizationFlags()) {
        this->cloneAndRegisterAllChildProcessors(src);
}
std::unique_ptr<GrFragmentProcessor> GrFunction::clone() const {
    return std::make_unique<GrFunction>(*this);
}
#if GR_TEST_UTILS
SkString GrFunction::onDumpInfo() const {
    return SkString();
}
#endif

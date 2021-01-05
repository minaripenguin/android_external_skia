

/**************************************************************************************************
 *** This file was autogenerated from GrChildProcessorWithInputExpression.fp; do not modify.
 **************************************************************************************************/
#include "GrChildProcessorWithInputExpression.h"

#include "src/core/SkUtils.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"
#include "src/gpu/glsl/GrGLSLProgramBuilder.h"
#include "src/sksl/SkSLCPP.h"
#include "src/sksl/SkSLUtil.h"
class GrGLSLChildProcessorWithInputExpression : public GrGLSLFragmentProcessor {
public:
    GrGLSLChildProcessorWithInputExpression() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrChildProcessorWithInputExpression& _outer = args.fFp.cast<GrChildProcessorWithInputExpression>();
        (void) _outer;
        colorVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag, kHalf4_GrSLType, "color");
        SkString _input0 = SkStringPrintf("%s * half4(0.5)", args.fUniformHandler->getUniformCStr(colorVar));
        SkString _sample0 = this->invokeChild(0, _input0.c_str(), args);
        fragBuilder->codeAppendf(
R"SkSL(return %s;
)SkSL"
, _sample0.c_str());
    }
private:
    void onSetData(const GrGLSLProgramDataManager& pdman, const GrFragmentProcessor& _proc) override {
    }
    UniformHandle colorVar;
};
GrGLSLFragmentProcessor* GrChildProcessorWithInputExpression::onCreateGLSLInstance() const {
    return new GrGLSLChildProcessorWithInputExpression();
}
void GrChildProcessorWithInputExpression::onGetGLSLProcessorKey(const GrShaderCaps& caps, GrProcessorKeyBuilder* b) const {
}
bool GrChildProcessorWithInputExpression::onIsEqual(const GrFragmentProcessor& other) const {
    const GrChildProcessorWithInputExpression& that = other.cast<GrChildProcessorWithInputExpression>();
    (void) that;
    return true;
}
GrChildProcessorWithInputExpression::GrChildProcessorWithInputExpression(const GrChildProcessorWithInputExpression& src)
: INHERITED(kGrChildProcessorWithInputExpression_ClassID, src.optimizationFlags()) {
        this->cloneAndRegisterAllChildProcessors(src);
}
std::unique_ptr<GrFragmentProcessor> GrChildProcessorWithInputExpression::clone() const {
    return std::make_unique<GrChildProcessorWithInputExpression>(*this);
}
#if GR_TEST_UTILS
SkString GrChildProcessorWithInputExpression::onDumpInfo() const {
    return SkString();
}
#endif

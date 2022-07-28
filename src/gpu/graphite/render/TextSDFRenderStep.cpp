/*
 * Copyright 2022 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/render/TextSDFRenderStep.h"

#include "src/core/SkPipelineData.h"

#include "include/gpu/graphite/Recorder.h"
#include "include/private/SkSLString.h"
#include "src/gpu/graphite/DrawParams.h"
#include "src/gpu/graphite/DrawWriter.h"
#include "src/gpu/graphite/RecorderPriv.h"
#include "src/gpu/graphite/text/AtlasManager.h"
#include "src/text/gpu/SubRunContainer.h"

namespace skgpu::graphite {

namespace {
static constexpr DepthStencilSettings kDirectShadingPass = {
        /*frontStencil=*/{},
        /*backStencil=*/ {},
        /*refValue=*/    0,
        /*stencilTest=*/ false,
        /*depthCompare=*/CompareOp::kGEqual,
        /*depthTest=*/   true,
        /*depthWrite=*/  true
};

// We are expecting to sample from up to 4 textures
constexpr int kNumSDFAtlasTextures = 4;
}  // namespace

TextSDFRenderStep::TextSDFRenderStep(bool isA8)
        : RenderStep("TextSDFRenderStep",
                     isA8 ? "A8" : "565",
                     Flags::kPerformsShading | Flags::kHasTextures | Flags::kEmitsCoverage,
                     /*uniforms=*/{{"atlasSizeInv", SkSLType::kFloat2},
                                   {"distanceAdjust", SkSLType::kFloat}},
                     PrimitiveType::kTriangles,
                     kDirectShadingPass,
                     /*vertexAttrs=*/
                     {{"position", VertexAttribType::kFloat2, SkSLType::kFloat2},
                      {"depth", VertexAttribType::kFloat, SkSLType::kFloat},
                      {"texCoords", VertexAttribType::kUShort2, SkSLType::kUShort2}},
                     /*instanceAttrs=*/{},
                     /*varyings=*/
                     {{"unormTexCoords", SkSLType::kFloat2},
                      {"textureCoords", SkSLType::kFloat2},
                      {"texIndex", SkSLType::kFloat}}) {
    // TODO: store if it's A8 and adjust shader
}

TextSDFRenderStep::~TextSDFRenderStep() {}

const char* TextSDFRenderStep::vertexSkSL() const {
    return R"(
        int2 coords = int2(texCoords.x, texCoords.y);
        int texIdx = coords.x >> 13;

        unormTexCoords = float2(coords.x & 0x1FFF, coords.y);
        textureCoords = unormTexCoords * atlasSizeInv;
        texIndex = float(texIdx);

        float4 devPosition = float4(position, depth, 1);
        )";
}

std::string TextSDFRenderStep::texturesAndSamplersSkSL(int binding) const {
    std::string result;

    for (unsigned int i = 0; i < kNumSDFAtlasTextures; ++i) {
        SkSL::String::appendf(&result,
                              "layout(binding=%d) uniform sampler2D sdf_atlas_%d;\n", binding, i);
        binding++;
    }

    return result;
}

const char* TextSDFRenderStep::fragmentCoverageSkSL() const {
    // TODO: To minimize the number of shaders generated this is the full affine shader.
    // For best performance it may be worth creating the uniform scale shader as well,
    // as that's the most common case.
    // TODO: Need to add 565 support.
    // TODO: Need aliased and possibly sRGB support.
    return R"(
        half texColor;
        if (texIndex == 0) {
           texColor = sample(sdf_atlas_0, textureCoords).r;
        } else if (texIndex == 1) {
           texColor = sample(sdf_atlas_1, textureCoords).r;
        } else if (texIndex == 2) {
           texColor = sample(sdf_atlas_2, textureCoords).r;
        } else if (texIndex == 3) {
           texColor = sample(sdf_atlas_3, textureCoords).r;
        } else {
           texColor = sample(sdf_atlas_0, textureCoords).r;
        }
        // The distance field is constructed as uchar8_t values, so that the zero value is at 128,
        // and the supported range of distances is [-4 * 127/128, 4].
        // Hence to convert to floats our multiplier (width of the range) is 4 * 255/128 = 7.96875
        // and zero threshold is 128/255 = 0.50196078431.
        half distance = 7.96875*(texColor - 0.50196078431);

        // We may further adjust the distance for gamma correction.
        distance -= half(distanceAdjust);

        // After the distance is unpacked, we need to correct it by a factor dependent on the
        // current transformation. For general transforms, to determine the amount of correction
        // we multiply a unit vector pointing along the SDF gradient direction by the Jacobian of
        // unormTexCoords (which is the inverse transform for this fragment) and take the length of
        // the result.
        half2 dist_grad = half2(float2(dFdx(distance), dFdy(distance)));
        half dg_len2 = dot(dist_grad, dist_grad);

        // The length of the gradient may be near 0, so we need to check for that. This also
        // compensates for the Adreno, which likes to drop tiles on division by 0
        if (dg_len2 < 0.0001) {
            dist_grad = half2(0.7071, 0.7071);
        } else {
            dist_grad = dist_grad*half(inversesqrt(dg_len2));
        }

        // Computing the Jacobian and multiplying by the gradient.
        half2 Jdx = half2(dFdx(unormTexCoords));
        half2 Jdy = half2(dFdy(unormTexCoords));
        half2 grad = half2(dist_grad.x*Jdx.x + dist_grad.y*Jdy.x,
                           dist_grad.x*Jdx.y + dist_grad.y*Jdy.y);

        // This gives us a smooth step across approximately one fragment.
        half afwidth = 0.65*length(grad);
        // TODO: handle aliased and sRGB rendering
        half val = smoothstep(-afwidth, afwidth, distance);
        outputCoverage = half4(val);
    )";
}

void TextSDFRenderStep::writeVertices(DrawWriter* dw, const DrawParams& params) const {
    const SubRunData& subRunData = params.geometry().subRunData();
    subRunData.subRun()->fillVertexData(dw, subRunData.startGlyphIndex(), subRunData.glyphCount(),
                                        params.order().depthAsFloat(), params.transform());
}

void TextSDFRenderStep::writeUniformsAndTextures(const DrawParams& params,
                                                 SkPipelineDataGatherer* gatherer) const {
    SkDEBUGCODE(UniformExpectationsValidator uev(gatherer, this->uniforms());)

    const SubRunData& subRunData = params.geometry().subRunData();
    unsigned int numProxies;
    Recorder* recorder = subRunData.recorder();
    const sk_sp<TextureProxy>* proxies =
            recorder->priv().atlasManager()->getProxies(subRunData.subRun()->maskFormat(),
                                                        &numProxies);
    SkASSERT(proxies && numProxies > 0);

    // write uniforms
    skvx::float2 atlasDimensionsInverse = {1.f/proxies[0]->dimensions().width(),
                                           1.f/proxies[0]->dimensions().height()};
    gatherer->write(atlasDimensionsInverse);

    // TODO: get this from DistanceFieldAdjustTable and luminance color (set in SubRunData?)
    float gammaCorrection = 0.f;
    gatherer->write(gammaCorrection);

    // write textures and samplers
    const SkSamplingOptions kSamplingOptions(SkFilterMode::kLinear);
    constexpr SkTileMode kTileModes[2] = { SkTileMode::kClamp, SkTileMode::kClamp };
    for (unsigned int i = 0; i < numProxies; ++i) {
        gatherer->add(kSamplingOptions, kTileModes, proxies[i]);
    }
    // If the atlas has less than 4 active proxies we still need to set up samplers for the shader.
    for (unsigned int i = numProxies; i < kNumSDFAtlasTextures; ++i) {
        gatherer->add(kSamplingOptions, kTileModes, proxies[0]);
    }
}

}  // namespace skgpu::graphite
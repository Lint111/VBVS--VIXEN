#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeStack.h"
#include "Recipe/SdfRecipeEval.h"
#include "Recipe/VoxelDocumentFlattener.h"
#include "Recipe/generated/RecipeContainer.g.h"
#include "Recipe/generated/VoxelDocument.g.h"
#include <glm/glm.hpp>

using namespace Vixen::SVO;
using namespace Vixen::SVO::Recipe;
using namespace Yeroket::Sdf::Generated;

namespace {

// sample_tri_layer.vxd — vendored golden asset (Yeroket VoxelDocumentGoldenTests.cs
// BuildSampleTriLayerDoc, VDC1 design §6): 3 rule layers, all enabled, 1 channel.
//  "base"  Box(halfExtents=(1,1,1))                          op=Union (ignored, first layer)
//  "bulge" Sphere(radius=0.6)                                 op=SmoothUnion blendRadius=0.15
//  "cut"   Cylinder(halfHeight=1.5, radius=0.35)               op=Subtract
const char* kGoldenPath = SAMPLE_TRI_LAYER_VXD_PATH;

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return {};
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

std::string NameOf(const VoxelDocLayerHeader* h) {
    // nameBytes is NUL-padded UTF-8; construct a std::string up to the first NUL
    // (or the full 32 bytes if unterminated).
    const char* raw = reinterpret_cast<const char*>(h->nameBytes);
    size_t len = 0;
    while (len < sizeof(h->nameBytes) && raw[len] != '\0') ++len;
    return std::string(raw, len);
}

// --- instruction builders (mirror test_recipe_eval_parity.cpp conventions) ---
SdfInstruction sphereOp(float radius) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere; in.data[3] = radius; return in;
}
SdfInstruction boxOp(glm::vec3 halfExtents) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
    in.data[0] = halfExtents.x; in.data[1] = halfExtents.y; in.data[2] = halfExtents.z; return in;
}
SdfInstruction cylinderOp(float halfHeight, float radius) {
    SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Cylinder;
    in.data[0] = halfHeight; in.data[1] = radius; return in;
}

// --- independent analytic oracles (re-derived, not calling evalRecipe) ---
float boxDist(glm::vec3 p, glm::vec3 hext) {
    glm::vec3 d = glm::abs(p) - hext;
    return glm::length(glm::max(d, glm::vec3(0.0f)))
         + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
}
float sphereDist(glm::vec3 p, float r) { return glm::length(p) - r; }
float cylinderDist(glm::vec3 p, float halfH, float r) {
    glm::vec2 d(glm::length(glm::vec2(p.x, p.z)) - r, std::abs(p.y) - halfH);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
}

// --- reference combine formulas (canonical SdfCoreKernels.g.hpp, cited in
// VoxelDocumentFlattener.h): SmoothUnion(a,b,k), Subtract(a,b)=A minus B,
// Intersect(a,b), Union(a,b). Combine args here follow evalRecipe's stack
// order: b=top(cutter/B, the newly-pushed layer field), a=deeper(base/A,
// the accumulated composite so far) — i.e. reference(accumulated, newLayer).
float refUnion(float a, float b) { return glm::min(a, b); }
float refSmoothUnion(float a, float b, float k) {
    float h = glm::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return glm::mix(b, a, h) - k * h * (1.0f - h);
}
float refSubtract(float a, float b) { return glm::max(a, -b); }
float refIntersect(float a, float b) { return glm::max(a, b); }

} // namespace

// ===========================================================================
// 1. C++ golden decode — completes the tri-language golden (C# writer ->
//    Python decode digest -> this C++ decode). Values pinned from the
//    Yeroket golden builder source (VoxelDocumentGoldenTests.cs,
//    BuildSampleTriLayerDoc), append-only.
// ===========================================================================
TEST(VoxelDocumentDecode, SampleTriLayerGoldenDecodesToExpectedValues) {
    auto bytes = ReadFile(kGoldenPath);
    ASSERT_FALSE(bytes.empty()) << "could not read golden: " << kGoldenPath;

    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(bytes.data(), bytes.size(), view));

    EXPECT_EQ(view.header.magic, 0x31434456u);       // 'VDC1'
    EXPECT_EQ(view.header.formatVersion, 1u);
    EXPECT_EQ(view.header.channelCount, 1u);
    EXPECT_EQ(view.header.layerCount, 3u);

    ASSERT_NE(view.channels, nullptr);
    EXPECT_EQ(view.channels[0].semanticId, 0u);          // SEM_SDF
    EXPECT_EQ(view.channels[0].elemCount, 1u);
    EXPECT_EQ(view.channels[0].channelBaseFloats, 0u);
    EXPECT_EQ(view.channels[0].fieldKind, 1u);           // FK_DISTANCE

    // Layer 0: "base" — Box(1,1,1), op=Union(0, ignored), enabled, blendRadius=0.
    {
        const auto* h = view.layers[0].header;
        EXPECT_EQ(NameOf(h), "base");
        EXPECT_EQ(h->type, 0u);
        EXPECT_EQ(h->op, 0u);
        EXPECT_EQ(h->enabled, 1u);
        EXPECT_FLOAT_EQ(h->blendRadius, 0.0f);
        EXPECT_EQ(h->instructionCount, 1u);
        ASSERT_NE(view.layers[0].instructions, nullptr);
        const auto& in = view.layers[0].instructions[0];
        EXPECT_EQ(in.opCode, (uint8_t)SdfOpCode::Box);
        EXPECT_FLOAT_EQ(in.data[0], 1.0f);
        EXPECT_FLOAT_EQ(in.data[1], 1.0f);
        EXPECT_FLOAT_EQ(in.data[2], 1.0f);
    }

    // Layer 1: "bulge" — Sphere(radius=0.6), op=SmoothUnion(1), blendRadius=0.15.
    {
        const auto* h = view.layers[1].header;
        EXPECT_EQ(NameOf(h), "bulge");
        EXPECT_EQ(h->type, 0u);
        EXPECT_EQ(h->op, 1u);
        EXPECT_EQ(h->enabled, 1u);
        EXPECT_NEAR(h->blendRadius, 0.15f, 1e-6f);
        EXPECT_EQ(h->instructionCount, 1u);
        ASSERT_NE(view.layers[1].instructions, nullptr);
        const auto& in = view.layers[1].instructions[0];
        EXPECT_EQ(in.opCode, (uint8_t)SdfOpCode::Sphere);
        EXPECT_NEAR(in.data[3], 0.6f, 1e-6f);
    }

    // Layer 2: "cut" — Cylinder(halfHeight=1.5, radius=0.35), op=Subtract(2).
    {
        const auto* h = view.layers[2].header;
        EXPECT_EQ(NameOf(h), "cut");
        EXPECT_EQ(h->type, 0u);
        EXPECT_EQ(h->op, 2u);
        EXPECT_EQ(h->enabled, 1u);
        EXPECT_FLOAT_EQ(h->blendRadius, 0.0f);
        EXPECT_EQ(h->instructionCount, 1u);
        ASSERT_NE(view.layers[2].instructions, nullptr);
        const auto& in = view.layers[2].instructions[0];
        EXPECT_EQ(in.opCode, (uint8_t)SdfOpCode::Cylinder);
        EXPECT_NEAR(in.data[0], 1.5f, 1e-6f);
        EXPECT_NEAR(in.data[1], 0.35f, 1e-6f);
    }
}

// ===========================================================================
// 2. Flatten grid parity — the authoritative oracle. Flatten the golden
//    document, evaluate the resulting VRC1 program with evalRecipe, and
//    compare against an INDEPENDENT composition: each layer's raw program
//    evaluated directly via evalRecipe (never through the flattener), then
//    combined pointwise with reference formulas re-derived from
//    SdfCoreKernels.g.hpp (cited in VoxelDocumentFlattener.h). This is not
//    circular — the flattener's output and the oracle are built two
//    different ways from the same raw per-layer instructions.
// ===========================================================================
TEST(VoxelDocumentFlatten, GridParityAgainstIndependentComposition) {
    auto bytes = ReadFile(kGoldenPath);
    ASSERT_FALSE(bytes.empty());
    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(bytes.data(), bytes.size(), view));

    std::vector<uint8_t> blob;
    std::string err;
    ASSERT_TRUE(FlattenVoxelDocument(view, nullptr, blob, err)) << err;

    RecipeContainerView flatView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadRecipeContainer(blob.data(), blob.size(), flatView));

    // Independent per-layer program extraction (built directly from the raw
    // instructions the golden decode test already pinned — not via the
    // flattener's internal accumulation).
    const glm::vec3 boxHalf(1.0f, 1.0f, 1.0f);
    const float sphereR = 0.6f;
    const float cylHalfH = 1.5f, cylR = 0.35f;
    const float blendK = 0.15f;

    SdfInstruction baseProg[]  = { boxOp(boxHalf) };
    SdfInstruction bulgeProg[] = { sphereOp(sphereR) };
    SdfInstruction cutProg[]   = { cylinderOp(cylHalfH, cylR) };

    std::vector<glm::vec3> samplePoints;
    for (float x = -2.0f; x <= 2.001f; x += 1.0f)
        for (float y = -2.0f; y <= 2.001f; y += 1.0f)
            for (float z = -2.0f; z <= 2.001f; z += 1.0f)
                samplePoints.emplace_back(x, y, z);
    ASSERT_EQ(samplePoints.size(), 5u * 5u * 5u);

    for (const glm::vec3& p : samplePoints) {
        const float flattened = evalRecipe(flatView.instructions, flatView.header.instructionCount, p);

        const float dBase  = evalRecipe(baseProg, 1, p);
        const float dBulge = evalRecipe(bulgeProg, 1, p);
        const float dCut   = evalRecipe(cutProg, 1, p);

        // base -> SmoothUnion(base, bulge, k) -> Subtract(that, cut)
        const float afterBulge = refSmoothUnion(dBase, dBulge, blendK);
        const float expected   = refSubtract(afterBulge, dCut);

        EXPECT_NEAR(flattened, expected, 1e-5f)
            << "at (" << p.x << "," << p.y << "," << p.z << ")";
    }
}

// Cheap sanity on the reference formulas themselves against Union/Intersect
// (not exercised by the golden asset's ops) so refUnion/refIntersect aren't
// dead code silently drifting from evalRecipe's SdfCore_* semantics.
TEST(VoxelDocumentFlatten, ReferenceFormulasMatchEvalRecipeForUnionAndIntersect) {
    SdfInstruction a = sphereOp(1.0f);
    SdfInstruction b = boxOp(glm::vec3(0.5f, 0.5f, 0.5f));

    SdfInstruction unionProg[]     = { a, b, [] { SdfInstruction i{}; i.opCode=(uint8_t)SdfOpCode::Union; return i; }() };
    SdfInstruction intersectProg[] = { a, b, [] { SdfInstruction i{}; i.opCode=(uint8_t)SdfOpCode::Intersect; return i; }() };

    for (glm::vec3 p : { glm::vec3(0,0,0), glm::vec3(0.7f,0,0), glm::vec3(2,2,2) }) {
        const float da = evalRecipe(&a, 1, p);
        const float db = evalRecipe(&b, 1, p);
        EXPECT_NEAR(evalRecipe(unionProg, 3, p), refUnion(da, db), 1e-5f);
        EXPECT_NEAR(evalRecipe(intersectProg, 3, p), refIntersect(da, db), 1e-5f);
    }
}

// ===========================================================================
// 3. enabledOverride semantics — flattening with the cut layer disabled via
//    override must byte-compare equal to flattening a document whose "cut"
//    layer enabled flag is cleared at the byte level.
// ===========================================================================
TEST(VoxelDocumentFlatten, EnabledOverrideMatchesByteLevelDisable) {
    auto originalBytes = ReadFile(kGoldenPath);
    ASSERT_FALSE(originalBytes.empty());

    // Path A: override vector, cut (layer index 2) disabled.
    VoxelDocumentView viewA{};
    ASSERT_TRUE(ReadVoxelDocument(originalBytes.data(), originalBytes.size(), viewA));
    std::vector<uint8_t> overrideVec = {1, 1, 0};   // base, bulge enabled; cut disabled
    std::vector<uint8_t> blobA;
    std::string errA;
    ASSERT_TRUE(FlattenVoxelDocument(viewA, &overrideVec, blobA, errA)) << errA;

    // Path B: byte-level disable. VoxelDocLayerHeader.enabled sits at offset 2
    // within each 48-byte layer header (type=0, op=1, enabled=2 — per the
    // vendored VoxelDocLayerHeader field order in VoxelDocument.g.h).
    std::vector<uint8_t> mutatedBytes = originalBytes;
    {
        VoxelDocumentView probe{};
        ASSERT_TRUE(ReadVoxelDocument(originalBytes.data(), originalBytes.size(), probe));
        const uint8_t* headerBytePtr = reinterpret_cast<const uint8_t*>(probe.layers[2].header);
        const size_t offset = static_cast<size_t>(headerBytePtr - originalBytes.data());
        ASSERT_LT(offset + 2, mutatedBytes.size());
        ASSERT_EQ(mutatedBytes[offset + 2], 1) << "expected 'cut' layer enabled byte == 1 before mutation";
        mutatedBytes[offset + 2] = 0;   // clear enabled
    }
    VoxelDocumentView viewB{};
    ASSERT_TRUE(ReadVoxelDocument(mutatedBytes.data(), mutatedBytes.size(), viewB));
    ASSERT_EQ(viewB.layers[2].header->enabled, 0u);
    std::vector<uint8_t> blobB;
    std::string errB;
    ASSERT_TRUE(FlattenVoxelDocument(viewB, nullptr, blobB, errB)) << errB;

    EXPECT_EQ(blobA, blobB);

    // Sanity: both should be shorter than the full 3-layer flatten (no
    // Subtract-cut combine instruction).
    std::vector<uint8_t> fullBlob;
    std::string fullErr;
    VoxelDocumentView fullView{};
    ASSERT_TRUE(ReadVoxelDocument(originalBytes.data(), originalBytes.size(), fullView));
    ASSERT_TRUE(FlattenVoxelDocument(fullView, nullptr, fullBlob, fullErr)) << fullErr;
    EXPECT_LT(blobA.size(), fullBlob.size());
}

// ===========================================================================
// 4. Error paths.
// ===========================================================================
TEST(VoxelDocumentFlatten, ZeroEnabledLayersFails) {
    auto originalBytes = ReadFile(kGoldenPath);
    ASSERT_FALSE(originalBytes.empty());
    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(originalBytes.data(), originalBytes.size(), view));

    std::vector<uint8_t> allDisabled = {0, 0, 0};
    std::vector<uint8_t> blob;
    std::string err;
    EXPECT_FALSE(FlattenVoxelDocument(view, &allDisabled, blob, err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(blob.empty());
}

TEST(VoxelDocumentFlatten, UnknownLayerOpFails) {
    // Build a synthetic 1-layer document with an out-of-range layer op (4)
    // via the vendored C++ writer (WriteVoxelDocument), so this is an
    // in-band malformed document, not hand-crafted bytes.
    SdfInstruction sphere = sphereOp(1.0f);
    VoxelDocLayerHeader layerHeader{};
    layerHeader.type = 0;
    layerHeader.op = 4;       // outside {0=union,1=smooth_union,2=subtract,3=intersect}
    layerHeader.enabled = 1;
    layerHeader.blendRadius = 0.0f;
    std::memcpy(layerHeader.nameBytes, "solo", 4);
    layerHeader.instructionCount = 1;

    VoxelDocChannel channel{};
    channel.semanticId = 0; channel.elemCount = 1; channel.channelBaseFloats = 0; channel.fieldKind = 1;

    VoxelDocLayerWrite layerWrite{ layerHeader, &sphere };

    std::vector<uint8_t> buf(4096);
    size_t outLen = 0;
    ASSERT_TRUE(WriteVoxelDocument(&channel, 1, &layerWrite, 1, buf.data(), buf.size(), outLen));
    buf.resize(outLen);

    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(buf.data(), buf.size(), view));

    // Single-layer document: op=4 is on the FIRST enabled layer, which is
    // treated as the base field (its op is ignored per design §5) — so a
    // single malformed-op layer alone would NOT surface the error. Add a
    // second enabled layer so the malformed op is exercised as a combine.
    SdfInstruction box = boxOp(glm::vec3(1,1,1));
    VoxelDocLayerHeader baseHeader{};
    baseHeader.type = 0; baseHeader.op = 0; baseHeader.enabled = 1; baseHeader.blendRadius = 0.0f;
    std::memcpy(baseHeader.nameBytes, "base", 4);
    baseHeader.instructionCount = 1;
    VoxelDocLayerWrite writes[2] = {
        { baseHeader, &box },
        { layerHeader, &sphere },   // op=4, invalid
    };
    std::vector<uint8_t> buf2(4096);
    size_t outLen2 = 0;
    ASSERT_TRUE(WriteVoxelDocument(&channel, 1, writes, 2, buf2.data(), buf2.size(), outLen2));
    buf2.resize(outLen2);
    VoxelDocumentView view2{};
    ASSERT_TRUE(ReadVoxelDocument(buf2.data(), buf2.size(), view2));

    std::vector<uint8_t> blob;
    std::string err;
    EXPECT_FALSE(FlattenVoxelDocument(view2, nullptr, blob, err));
    EXPECT_FALSE(err.empty());
}

TEST(VoxelDocumentFlatten, DeepStackSyntheticDocumentRejectedWithClearMessage) {
    // 70 enabled layers, each a single Box instruction, op=Union between
    // consecutive layers. Each combine nets the value stack back down (push
    // 1, then pop 2/push 1), so plain Union chaining alone would never
    // overflow sp. To actually blow the shared VM's 64-slot guard we give
    // each layer TWO Box instructions with no combine within the layer's
    // own program (each layer program pushes 2 without popping); the
    // combine after each layer only pops 2/pushes 1 net -1 per layer, so the
    // net per layer is +1 on the value stack. After ~65 layers sp exceeds 64.
    std::vector<SdfInstruction> perLayerInstrs = { boxOp(glm::vec3(1,1,1)), boxOp(glm::vec3(1,1,1)) };

    constexpr uint32_t kLayerCount = 70;
    std::vector<VoxelDocLayerHeader> headers(kLayerCount);
    std::vector<VoxelDocLayerWrite> writes(kLayerCount);
    for (uint32_t i = 0; i < kLayerCount; ++i) {
        headers[i] = VoxelDocLayerHeader{};
        headers[i].type = 0;
        headers[i].op = 0;   // union
        headers[i].enabled = 1;
        headers[i].blendRadius = 0.0f;
        std::string name = "L" + std::to_string(i);
        std::memcpy(headers[i].nameBytes, name.data(), std::min<size_t>(name.size(), sizeof(headers[i].nameBytes)));
        headers[i].instructionCount = static_cast<uint32_t>(perLayerInstrs.size());
        writes[i] = VoxelDocLayerWrite{ headers[i], perLayerInstrs.data() };
    }

    VoxelDocChannel channel{};
    channel.semanticId = 0; channel.elemCount = 1; channel.channelBaseFloats = 0; channel.fieldKind = 1;

    std::vector<uint8_t> buf(1 << 20);
    size_t outLen = 0;
    ASSERT_TRUE(WriteVoxelDocument(&channel, 1, writes.data(), kLayerCount, buf.data(), buf.size(), outLen))
        << "synthetic document write buffer too small (need " << outLen << ")";
    buf.resize(outLen);

    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(buf.data(), buf.size(), view));
    EXPECT_EQ(view.header.layerCount, kLayerCount);

    std::vector<uint8_t> blob;
    std::string err;
    EXPECT_FALSE(FlattenVoxelDocument(view, nullptr, blob, err));
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("stack"), std::string::npos) << "err should name the stack-depth guard: " << err;
    EXPECT_TRUE(blob.empty());
}

// ===========================================================================
// 5. Registry-consumable — the vendored ReadRecipeContainer accepts the
//    flatten output, and instructionCount matches the documented formula.
// ===========================================================================
TEST(VoxelDocumentFlatten, OutputIsRegistryConsumableWithExpectedInstructionCount) {
    auto bytes = ReadFile(kGoldenPath);
    ASSERT_FALSE(bytes.empty());
    VoxelDocumentView view{};
    ASSERT_TRUE(ReadVoxelDocument(bytes.data(), bytes.size(), view));

    std::vector<uint8_t> blob;
    std::string err;
    ASSERT_TRUE(FlattenVoxelDocument(view, nullptr, blob, err)) << err;

    RecipeContainerView flatView{};
    ASSERT_TRUE(Yeroket::Sdf::Generated::ReadRecipeContainer(blob.data(), blob.size(), flatView))
        << "flattened blob must be consumable by the vendored VRC1 reader unchanged";

    // 3 enabled layers, 1 instruction each = 3 program instructions,
    // + (enabledCount-1)=2 combine instructions = 5 total.
    uint32_t enabledInstrSum = 0;
    for (uint32_t l = 0; l < view.header.layerCount; ++l)
        if (view.layers[l].header->enabled)
            enabledInstrSum += view.layers[l].header->instructionCount;
    const uint32_t enabledCount = 3;
    const uint32_t expected = enabledInstrSum + (enabledCount - 1);

    EXPECT_EQ(flatView.header.instructionCount, expected);
    EXPECT_EQ(flatView.header.instructionCount, 5u);

    // RecipeRegistry::Register must also accept it (fully registry-consumable,
    // not just container-readable).
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry entry{};
    entry.bakeResolution = flatView.header.bakeResolution;
    entry.bandVoxels     = flatView.header.bandVoxels;
    entry.brickDepth     = flatView.header.brickDepth;
    entry.bytecode.assign(flatView.instructions, flatView.instructions + flatView.header.instructionCount);
    EXPECT_EQ(reg.Register(1u, entry), RecipeRegistry::RegisterResult::Ok);
}

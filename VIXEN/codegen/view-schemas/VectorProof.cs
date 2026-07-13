using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    // View Contract Inc-5b Milestone 2.4b: a minimal, standalone proof schema for ViewFieldKind.
    // Vector emission -- NOT the native Hud schema (would perturb its version hash / existing
    // 48/48-style proof, per Milestone 2.4a's recommendation). A single Float3-marker-typed scalar
    // field, matching Bodies.Position/RecipeParams's real shape exactly, proves the full round-trip
    // (write via <VectorProof>ViewWriter -> decode via ViewWireReaderSoa::Apply -> read via the
    // generated VectorProofSection accessor) without touching Bodies' own deferred declaration.
    [View]
    public struct VectorProof
    {
        public Float3 position;
    }
}

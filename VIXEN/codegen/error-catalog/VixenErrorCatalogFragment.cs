using System.Collections.Generic;
using CodegenTool.ErrorCatalog;

namespace Vixen.ErrorCatalog;

/// <summary>VIXEN-owned renderer diagnostics. Vulkan recovery policy remains in the native adapter.</summary>
public sealed class VixenErrorCatalogFragment : ErrorCatalogFragment
{
    public override string Namespace => "vixen";

    public override IReadOnlyList<ErrorCatalogEntry> Definitions => new[]
    {
        ErrorCatalogEntry.Define(new ErrorDefinition(
            "vixen.renderer.device-lost",
            new[] { "VIXEN001" },
            "vixen",
            ErrorSeverity.Fatal,
            ErrorPhase.Runtime,
            new ErrorText("The renderer lost its Vulkan device after {result}."),
            new ErrorText("The device or driver stopped accepting work and the renderer cannot safely reuse its handles."),
            new ErrorText("Recreate the renderer device and its dependent resources before submitting more work."),
            new[] { new ErrorParameterDefinition("result", ErrorParameterType.Identifier) },
            "vixen-renderer-device-lost",
            DisplayCode: "VIXEN001")),
        ErrorCatalogEntry.Define(new ErrorDefinition(
            "vixen.renderer.swapchain-out-of-date",
            new[] { "VIXEN002" },
            "vixen",
            ErrorSeverity.Warning,
            ErrorPhase.Runtime,
            new ErrorText("The renderer swapchain cannot present after {result}."),
            new ErrorText("The surface dimensions or presentation state changed while the renderer was presenting."),
            new ErrorText("Recreate the swapchain and its image-dependent resources, then retry the frame."),
            new[] { new ErrorParameterDefinition("result", ErrorParameterType.Identifier) },
            "vixen-renderer-swapchain-out-of-date",
            DisplayCode: "VIXEN002")),
        ErrorCatalogEntry.Define(new ErrorDefinition(
            "vixen.renderer.out-of-device-memory",
            new[] { "VIXEN003" },
            "vixen",
            ErrorSeverity.Error,
            ErrorPhase.Runtime,
            new ErrorText("The renderer could not allocate device memory after {result}."),
            new ErrorText("The device-local memory budget is exhausted or the requested allocation is too large."),
            new ErrorText("Degrade the workload or release optional GPU resources before retrying the allocation."),
            new[] { new ErrorParameterDefinition("result", ErrorParameterType.Identifier) },
            "vixen-renderer-out-of-device-memory",
            DisplayCode: "VIXEN003")),
        ErrorCatalogEntry.Define(new ErrorDefinition(
            "vixen.renderer.shader-compile-failed",
            new[] { "VIXEN004" },
            "vixen",
            ErrorSeverity.Error,
            ErrorPhase.Runtime,
            new ErrorText("Renderer shader {shader} failed to compile."),
            new ErrorText("The shader source or target device capabilities do not satisfy the renderer pipeline contract."),
            new ErrorText("Disable the affected feature and rebuild or replace {shader} before retrying the pipeline."),
            new[] { new ErrorParameterDefinition("shader", ErrorParameterType.Identifier) },
            "vixen-renderer-shader-compile-failed",
            DisplayCode: "VIXEN004")),
    };
}

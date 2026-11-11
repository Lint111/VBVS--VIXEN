using System.Collections.Generic;
using CodegenTool.ErrorCatalog;

namespace Vixen.ErrorCatalog.Fixtures;

/// <summary>Intentional negative fixture: PIPE001 is owned by the kernel fragment.</summary>
public sealed class VixenAliasCollision : ErrorCatalogFragment
{
    public override string Namespace => "vixen";

    public override IReadOnlyList<ErrorCatalogEntry> Definitions => new[]
    {
        ErrorCatalogEntry.Define(new ErrorDefinition(
            "vixen.fixture.kernel-alias-collision",
            new[] { "PIPE001" },
            "vixen",
            ErrorSeverity.Error,
            ErrorPhase.Generate,
            new ErrorText("The VIXEN collision fixture is intentionally invalid."),
            new ErrorText("The fixture claims a kernel-owned alias."),
            new ErrorText("Rename the fixture alias before compiling the catalog."),
            new ErrorParameterDefinition[0],
            "vixen-fixture-kernel-alias-collision",
            DisplayCode: "VIXEN999")),
    };
}

using System;

namespace Vixen.Codegen.Attributes;

/// <summary>Layout convention a GPU struct is packed with.</summary>
public enum GpuLayout { Std430 }

/// <summary>Marks a C# struct as the single source of a GPU config struct.
/// The codegen tool emits byte-identical C++ and GLSL from it.</summary>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false)]
public sealed class GpuStructAttribute : System.Attribute
{
    public GpuLayout Layout { get; set; } = GpuLayout.Std430;
}

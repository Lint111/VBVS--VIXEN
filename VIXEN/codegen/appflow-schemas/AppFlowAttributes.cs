using System;

// Marker attributes for the AppFlow Tier-2 declaration surface (design §4.1).
// NOTE (Task 1 decision, recorded in AppFlowReference.cs): the existing Yeroket
// `CodegenTool~` only understands `[GpuStruct]` (struct layout → C++/GLSL struct
// body); it has no enum/table/reader emitter. These attributes are declared here
// so the reference vocabulary is well-formed, buildable C# and so a future
// enum+table emitter (the Task-1-deferred follow-up) has a fixed surface to
// target — they are not consumed by any tool yet.
namespace Vixen.AppFlow.Reference
{
    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowStateEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowGuardEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowActionEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowParamTypeEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowActionParamsAttribute : Attribute
    {
        public string ActionName { get; }
        public FlowActionParamsAttribute(string actionName) { ActionName = actionName; }
    }

    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class FlowStateStructAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowTransitionAttribute : Attribute { }
}

using System;

// Marker attributes for the AppFlow Tier-2 declaration surface (design §4.1).
// Reflected by Yeroket's AppFlowEmitter ($KF/SourceGenerator~/Transpiler/AppFlowEmitter.cs)
// to generate AppFlow.g.h from AppFlowReference.cs (Inc-4 M1). Declared VIXEN-side (not in
// Yeroket's GpuStructAttributes.cs) because the reference vocabulary itself lives here.
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

    // Inc-4 M2: typed key vocabulary + element/key/return-edge declaration surface (design §3.1/§5.2).
    public enum FlowScope { Global = 0, State = 1, Context = 2 }

    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowKeyEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Enum)]
    public sealed class FlowModEnumAttribute : Attribute { }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowElementTriggerAttribute : Attribute
    {
        public string ActionName { get; }
        public FlowElementTriggerAttribute(string action) { ActionName = action; }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowKeyDefaultAttribute : Attribute
    {
        public string ActionName { get; }
        public FlowScope Scope { get; }
        public string State { get; }
        public FlowKeyDefaultAttribute(string action, FlowScope scope, string state = null)
        {
            ActionName = action;
            Scope = scope;
            State = state;
        }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowReturnEdgeAttribute : Attribute
    {
        public string FromState { get; }
        public FlowReturnEdgeAttribute(string from) { FromState = from; }
    }

    [AttributeUsage(AttributeTargets.Class)]
    public sealed class FlowEdgeEffectAttribute : Attribute
    {
        public int TransitionIndex { get; }
        public FlowEdgeEffectAttribute(int transition) { TransitionIndex = transition; }
    }
}

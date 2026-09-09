// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using ILCompiler.DependencyAnalysis.ARM;

namespace ILCompiler.DependencyAnalysis
{
    public partial class UnboxingStubNode
    {
        protected override void EmitCode(NodeFactory factory, ref ARMEmitter encoder, bool relocsOnly)
        {
            encoder.EmitADD(encoder.TargetRegister.Arg0, (byte)factory.Target.PointerSize); // add r0, sizeof(void*);
            if (factory.Target.IsApplePlatform)
            {
                // __unbox may be more than 16 MiB away from the managed method.
                encoder.EmitMOV(Register.R12, GetUnderlyingMethodEntrypoint(factory));
                encoder.EmitJMP(Register.R12);
                // MOV32's PC base lies after the BX. Keep that address inside
                // this atom, including the final unboxing stub in the section.
                encoder.EmitNOP();
            }
            else
            {
                encoder.EmitJMP(GetUnderlyingMethodEntrypoint(factory)); // b methodEntryPoint
            }
        }
    }
}

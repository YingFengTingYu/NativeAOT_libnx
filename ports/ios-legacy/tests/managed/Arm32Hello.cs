// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Runtime.InteropServices;

internal static class Arm32Hello
{
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Print", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Print();

    private static int Main()
    {
        Print();
        return IntPtr.Size == 4 ? 0 : 1;
    }
}

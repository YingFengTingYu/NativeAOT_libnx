// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Runtime.InteropServices;
using LegacyIOSExample;

Console.WriteLine($"NativeAOT project example: pointer={IntPtr.Size}, pid={Native.ProcessId()}");
if (Native.ProcessId() <= 0 || Shared.ReadMessage() != "embedded resource" || Shared.Checksum() != 0xCBF43926 ||
    File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "message.txt")).Trim() != "published content")
{
    throw new InvalidOperationException("Project reference, NuGet, source generator, resource or content check failed");
}
Console.WriteLine("PASS project / NuGet / LibraryImport / resource / content");

internal static partial class Native
{
    [LibraryImport("/usr/lib/libSystem.B.dylib", EntryPoint = "getpid")]
    internal static partial int ProcessId();
}

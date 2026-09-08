// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.IO;
using System.IO.Hashing;
using System.Text;

namespace LegacyIOSExample;

public static class Shared
{
    public static string ReadMessage()
    {
        using Stream stream = typeof(Shared).Assembly.GetManifestResourceStream("LegacyIOSExample.Message")!;
        using StreamReader reader = new StreamReader(stream);
        return reader.ReadToEnd().Trim();
    }

    public static uint Checksum() => Crc32.HashToUInt32(Encoding.ASCII.GetBytes("123456789"));
}

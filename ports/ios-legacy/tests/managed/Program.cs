// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

internal static class Program
{
    [ThreadStatic]
    private static int s_threadMarker;
    private static int s_finalized;
    private static int s_callbackCount;
    private static int s_splitHighWord;
    private static int s_interopCallbackCount;

    private sealed class Finalizable
    {
        ~Finalizable() => Interlocked.Increment(ref s_finalized);
    }

    private interface IBoxedValue
    {
        int Read();
    }

    private struct BoxedValue : IBoxedValue
    {
        public int Value;
        public int Read() => Value;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int ReadBoxed(IBoxedValue value) => value.Read();

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static object[] InvalidArray(int count) => new object[count];

    private sealed class Holder
    {
        public int Value;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate double MixedCallback(long value, double a, double b, double c, double d,
        double e, double f, double g, double h);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate double OddDoubleCallback(int a, double b, int c);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_OddDouble", CallingConvention = CallingConvention.Cdecl)]
    private static extern double OddDouble(int a, double b, int c);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_SplitDouble", CallingConvention = CallingConvention.Cdecl)]
    private static extern double SplitDouble(int a, int b, int c, double d);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_OddInt64", CallingConvention = CallingConvention.Cdecl)]
    private static extern long OddInt64(int a, long b, int c);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_SplitInt64", CallingConvention = CallingConvention.Cdecl)]
    private static extern long SplitInt64(int a, int b, int c, long d);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_InvokeOddDouble", CallingConvention = CallingConvention.Cdecl)]
    private static extern double InvokeOddDouble(IntPtr callback);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_InvokeCallback", CallingConvention = CallingConvention.Cdecl)]
    private static extern double InvokeCallback(IntPtr callback, int foreignThread);

    [DllImport("__Internal", EntryPoint = "LegacyIOS_SetErrno", SetLastError = true)]
    private static extern int SetNativeError(int value);

    private static int Main(string[] args)
    {
        (string Name, Action Test)[] tests =
        [
            ("clock", CheckClock),
            ("gc", CheckGC),
            ("exceptions", CheckExceptions),
            ("threads", CheckThreads),
            ("callbacks", CheckCallbacks),
            ("interop", CheckInterop),
            ("tasks", CheckTasks),
            ("files", CheckFiles),
        ];
        Console.WriteLine("NativeAOT legacy iOS probe starting");
        try
        {
            int completed = 0;
            foreach ((string name, Action test) in tests)
            {
                if (args.Length != 0 && args[0] != name)
                    continue;
                Console.WriteLine($"BEGIN {name}");
                test();
                Console.WriteLine($"PASS {name}");
                completed++;
            }
            Check(completed != 0, "Unknown probe suite");
            Console.WriteLine($"PASS ALL ({completed} suites)");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"FAIL {error}");
            return 1;
        }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void CheckClock()
    {
        long before = Stopwatch.GetTimestamp();
        Thread.Sleep(30);
        long after = Stopwatch.GetTimestamp();
        Check(after > before, "Monotonic clock did not advance");
        Check(Stopwatch.GetElapsedTime(before, after).TotalMilliseconds >= 10, "Clock frequency is incorrect");
        Check(DateTime.UtcNow.Year >= 2020, "System time is invalid");
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void AllocateFinalizers()
    {
        for (int i = 0; i < 32; i++)
            _ = new Finalizable();
    }

    private static void CheckGC()
    {
        byte[][] retained = new byte[64][];
        for (int i = 0; i < retained.Length; i++)
        {
            retained[i] = new byte[4096 + i];
            retained[i][0] = (byte)i;
            retained[i][^1] = (byte)(255 - i);
        }
        AllocateFinalizers();
        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        GC.WaitForPendingFinalizers();
        for (int i = 0; i < retained.Length; i++)
            Check(retained[i][0] == i && retained[i][^1] == 255 - i, "GC corrupted retained objects");
        Check(Volatile.Read(ref s_finalized) >= 32, "Finalizers did not run");
        GC.KeepAlive(retained);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int ReadValue(Holder value) => value.Value;

    private static void CheckExceptions()
    {
        bool caught = false;
        try
        {
            throw new InvalidOperationException("explicit exception probe");
        }
        catch (InvalidOperationException error)
        {
            caught = error.Message == "explicit exception probe" && error.StackTrace is not null;
        }
        Check(caught, "Explicit exception or stack trace failed");
        caught = false;
        try
        {
            _ = ReadValue(null!);
        }
        catch (NullReferenceException)
        {
            caught = true;
        }
        Check(caught, "Null-reference exception failed");
        caught = false;
        try
        {
            _ = InvalidArray(-1);
        }
        catch (OverflowException)
        {
            caught = true;
        }
        Check(caught, "Allocation overflow helper failed");

        IBoxedValue boxed = new BoxedValue { Value = 42 };
        Check(ReadBoxed(boxed) == 42 && ReadBoxed(boxed) == 42, "Boxed interface dispatch or cache failed");
        Func<int> read = boxed.Read;
        Check(read() == 42 && read.Method.Name == "Read", "Boxed delegate or unboxing target lookup failed");
    }

    private static void CheckThreads()
    {
        s_threadMarker = -1;
        SetNativeError(77);
        for (int round = 0; round < 8; round++)
        {
            Thread[] threads = new Thread[4];
            Exception?[] errors = new Exception?[4];
            for (int i = 0; i < threads.Length; i++)
            {
                int index = i;
                threads[i] = new Thread(() =>
                {
                    try
                    {
                        Check(s_threadMarker == 0, "Thread statics were not initialized");
                        s_threadMarker = index + 100;
                        SetNativeError(index + 100);
                        Check(Marshal.GetLastPInvokeError() == index + 100, "Worker native error capture failed");
                        for (int iteration = 0; iteration < 20; iteration++)
                        {
                            byte[] data = new byte[16384];
                            data[0] = (byte)index;
                            if (iteration % 5 == 0)
                                GC.Collect();
                            Thread.Yield();
                            Check(s_threadMarker == index + 100 && data[0] == index, "Thread statics or GC isolation failed");
                        }
                    }
                    catch (Exception error)
                    {
                        errors[index] = error;
                    }
                }) { IsBackground = true };
                threads[i].Start();
            }
            for (int i = 0; i < threads.Length; i++)
            {
                Check(threads[i].Join(30000), "Managed thread did not exit");
                if (errors[i] is Exception error)
                    throw new InvalidOperationException("Managed worker failed", error);
            }
            Check(s_threadMarker == -1, "Worker overwrote main-thread statics");
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static long ForwardSplitArgument(int a, int b, int c, long value)
    {
        try
        {
            return SplitInt64(a, b, c, value);
        }
        finally
        {
            // Keep both halves live across a call and an EH region so the
            // incoming stack half needs a correct local home on Darwin ARM.
            Volatile.Write(ref s_splitHighWord, (int)(value >> 32));
        }
    }

    private static void CheckCallbacks()
    {
        const long marker = 0x1234567890ABCDE;
        Check(OddDouble(1, 2.5, 7) == 132, "P/Invoke double after an odd register slot failed");
        Check(SplitDouble(11, 22, 33, 2.5) == 68.5, "P/Invoke split double failed");
        Check(OddInt64(11, marker, 22) == marker + 33, "P/Invoke int64 after an odd register slot failed");
        Check(SplitInt64(11, 22, 33, marker) == marker + 66, "P/Invoke split int64 failed");
        Check(ForwardSplitArgument(11, 22, 33, marker) == marker + 66 &&
            Volatile.Read(ref s_splitHighWord) == (int)(marker >> 32), "Incoming split int64 parameter home failed");
        OddDoubleCallback oddCallback = (a, b, c) =>
        {
            GC.Collect();
            return a * 100 + b * 10 + c;
        };
        Check(InvokeOddDouble(Marshal.GetFunctionPointerForDelegate(oddCallback)) == 132,
            "Reverse P/Invoke odd double arguments failed");
        GC.KeepAlive(oddCallback);

        SetNativeError(77);
        Check(Marshal.GetLastPInvokeError() == 77, "Native error capture failed");
        Holder receiver = new Holder { Value = 37 };
        MixedCallback callback = (value, a, b, c, d, e, f, g, h) =>
        {
            GC.Collect();
            Interlocked.Increment(ref s_callbackCount);
            return value == receiver.Value ? value + a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h : -3;
        };
        IntPtr entry = Marshal.GetFunctionPointerForDelegate(callback);
        for (int i = 0; i < 16; i++)
        {
            Check(InvokeCallback(entry, 0) == 241, "Same-thread callback lost arguments or context");
            Check(InvokeCallback(entry, 1) == 241, "Foreign-thread callback lost arguments or context");
        }
        Check(s_callbackCount == 32, "Callback count is incorrect");
        GC.KeepAlive(callback);
        GC.KeepAlive(receiver);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Small { public byte Value; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Pair { public int X; public int Y; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Triple { public int X; public int Y; public int Z; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Float4 { public float X; public float Y; public float Z; public float W; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Double2 { public double X; public double Y; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Mixed { public int Tag; public double Value; public short Tail; }

#if LEGACY_IOS_ARM32
    private const int NativeStructPacking = 4;
#else
    private const int NativeStructPacking = 0;
#endif
    // UnmanagedCallersOnly performs no marshalling, so its types must describe
    // the native layout explicitly. DllImport/delegates above exercise the
    // automatic conversion from the ordinary managed Mixed representation.
    [StructLayout(LayoutKind.Sequential, Pack = NativeStructPacking)]
    private struct NativeMixed { public int Tag; public double Value; public short Tail; }

    private struct AtomicCounts { public long Value; }
    [StructLayout(LayoutKind.Explicit, Size = 128)]
    private struct SeparatedAtomicCounts
    {
        [FieldOffset(64)] public AtomicCounts Counts;
    }
    private sealed class AtomicHolder
    {
        public int Prefix;
        public SeparatedAtomicCounts Separated;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct Packed { public byte Tag; public double Value; public short Tail; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Nested { public Pair Pair; public Double2 Doubles; public int Tail; }

    [DllImport("__Internal", EntryPoint = "LegacyIOS_Layout")]
    private static extern int NativeLayout(int selector);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Small")]
    private static extern Small TransformSmall(Small value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Pair")]
    private static extern Pair TransformPair(int prefix, Pair value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Triple")]
    private static extern Triple TransformTriple(Triple value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_SplitTriple")]
    private static extern int SumSplitTriple(int a, int b, int c, Triple value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Float4")]
    private static extern Float4 TransformFloat4(int prefix, Float4 value, double scale);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Double2")]
    private static extern Double2 TransformDouble2(int prefix, Double2 value, float scale);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Mixed")]
    private static extern Mixed TransformMixed(Mixed value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Packed")]
    private static extern Packed TransformPacked(int prefix, Packed value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Nested")]
    private static extern Nested TransformNested(Nested value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_ByRef")]
    private static extern void TransformByRef(ref Mixed value, out Double2 result);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_Floats")]
    private static extern double ManyFloats(float a, double b, float c, double d, float e,
        double f, float g, double h, float i, double j, float k, double l);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_FloatIdentity")]
    private static extern float FloatIdentity(float value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_DoubleIdentity")]
    private static extern double DoubleIdentity(double value);
    [DllImport("__Internal", EntryPoint = "LegacyIOS_InvokeStructCallback")]
    private static extern int InvokeStructCallback(IntPtr callback, int foreignThread);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate Mixed StructCallback(int prefix, Float4 floats, Double2 doubles, Packed packed, long marker);

    private static Mixed StructCallbackBody(int prefix, Float4 floats, Double2 doubles, Packed packed, long marker)
    {
        // Never let a managed exception escape through a native caller.
        try
        {
            Check(prefix == 17 && floats.X == 1.25f && floats.Y == -2.5f && floats.Z == 3.75f && floats.W == 4.5f &&
                doubles.X == 8.25 && doubles.Y == -9.5 && packed.Tag == 0xAB && packed.Value == 12.5 &&
                packed.Tail == -1234 && marker == 0x123456789ABCDEF, "Struct callback arguments changed");
            byte[] retained = new byte[1024];
            retained[0] = 77;
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            Mixed result = TransformMixed(new Mixed { Tag = prefix, Value = 42.25, Tail = 123 });
            Check(retained[0] == 77, "Struct callback GC corrupted live object");
            Interlocked.Increment(ref s_interopCallbackCount);
            return result;
        }
        catch
        {
            return default;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static NativeMixed StaticStructCallback(int prefix, Float4 floats, Double2 doubles, Packed packed, long marker)
    {
        Mixed result = StructCallbackBody(prefix, floats, doubles, packed, marker);
        return new NativeMixed { Tag = result.Tag, Value = result.Value, Tail = result.Tail };
    }

    private static unsafe void CheckInterop()
    {
        Console.WriteLine("  interop: native layouts");
        int[] managedLayouts = [Marshal.SizeOf<Small>(), Marshal.SizeOf<Pair>(), Marshal.SizeOf<Triple>(),
            Marshal.SizeOf<Float4>(), Marshal.SizeOf<Double2>(), Marshal.SizeOf<Mixed>(),
            (int)Marshal.OffsetOf<Mixed>(nameof(Mixed.Value)), (int)Marshal.OffsetOf<Mixed>(nameof(Mixed.Tail)),
            Marshal.SizeOf<Packed>(), (int)Marshal.OffsetOf<Packed>(nameof(Packed.Value)), Marshal.SizeOf<Nested>()];
        for (int i = 0; i < managedLayouts.Length; i++)
        {
            int actual = NativeLayout(i);
            Check(actual == managedLayouts[i], $"Native layout {i}: C={actual}, C#={managedLayouts[i]}");
        }

        Console.WriteLine("  interop: struct parameters and returns");
        Check(TransformSmall(new Small { Value = 0xA5 }).Value == 0xFF, "One-byte struct return failed");
        Pair pair = TransformPair(7, new Pair { X = 11, Y = -22 });
        Check(pair.X == 18 && pair.Y == -29, "Eight-byte struct return failed");
        Triple triple = TransformTriple(new Triple { X = 11, Y = 22, Z = 33 });
        Check(triple.X == 33 && triple.Y == 11 && triple.Z == 22, "Twelve-byte struct return failed");
        Check(SumSplitTriple(1, 2, 3, new Triple { X = 11, Y = 22, Z = 33 }) == 160, "Split struct argument failed");
        Float4 floats = TransformFloat4(7, new Float4 { X = 1.25f, Y = -2.5f, Z = 3.75f, W = 4.5f }, 2);
        Check(floats.X == 9.5f && floats.Y == -12 && floats.Z == 7.5f && floats.W == 9, "Float HFA argument/return failed");
        Double2 doubles = TransformDouble2(3, new Double2 { X = 8.25, Y = -9.5 }, 2);
        Check(doubles.X == 19.5 && doubles.Y == -22, "Double HFA argument/return failed");
        Mixed mixed = TransformMixed(new Mixed { Tag = 17, Value = 42.25, Tail = 123 });
        Check(mixed.Tag == 18 && mixed.Value == 84.5 && mixed.Tail == 122, "Mixed struct argument/return failed");
        Packed packed = TransformPacked(7, new Packed { Tag = 0xAB, Value = 12.5, Tail = -1234 });
        Check(packed.Tag == 0xAC && packed.Value == 19.5 && packed.Tail == -1241, "Pack=1 struct argument/return failed");
        Nested nested = TransformNested(new Nested { Pair = new Pair { X = 11, Y = -22 },
            Doubles = new Double2 { X = 8.25, Y = -9.5 }, Tail = 31 });
        Check(nested.Pair.X == -22 && nested.Pair.Y == 11 && nested.Doubles.X == -9.5 &&
            nested.Doubles.Y == 8.25 && nested.Tail == 32, "Nested struct argument/return failed");
        TransformByRef(ref mixed, out doubles);
        Check(mixed.Tag == 19 && mixed.Value == 169 && mixed.Tail == 121 && doubles.X == 84.5 && doubles.Y == 169,
            "ref/out struct marshalling failed");

        Console.WriteLine("  interop: floating-point registers, stack and bit patterns");
        Check(ManyFloats(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == 650, "Mixed float/double register and stack arguments failed");
        Check(BitConverter.SingleToInt32Bits(FloatIdentity(BitConverter.Int32BitsToSingle(unchecked((int)0x80000000)))) == unchecked((int)0x80000000),
            "Float negative zero changed");
        Check(BitConverter.DoubleToInt64Bits(DoubleIdentity(BitConverter.Int64BitsToDouble(long.MinValue))) == long.MinValue,
            "Double negative zero changed");
        Check(float.IsNaN(FloatIdentity(float.NaN)) && double.IsPositiveInfinity(DoubleIdentity(double.PositiveInfinity)),
            "Special floating-point values changed");

        Console.WriteLine("  interop: delegate and UnmanagedCallersOnly struct callbacks with GC");
        int before = Volatile.Read(ref s_interopCallbackCount);
        Holder receiver = new Holder { Value = 99 };
        StructCallback callback = (prefix, f, d, p, marker) => receiver.Value == 99 ? StructCallbackBody(prefix, f, d, p, marker) : default;
        IntPtr delegateEntry = Marshal.GetFunctionPointerForDelegate(callback);
        IntPtr staticEntry = (IntPtr)(delegate* unmanaged[Cdecl]<int, Float4, Double2, Packed, long, NativeMixed>)&StaticStructCallback;
        for (int iteration = 0; iteration < 8; iteration++)
        {
            foreach (IntPtr entry in new[] { delegateEntry, staticEntry })
            {
                Check(InvokeStructCallback(entry, 0) == 1, "Same-thread struct callback failed");
                Check(InvokeStructCallback(entry, 1) == 1, "Foreign-thread struct callback failed");
            }
        }
        Check(Volatile.Read(ref s_interopCallbackCount) - before == 32, "Struct callback count is incorrect");
        GC.KeepAlive(callback);
        GC.KeepAlive(receiver);

        Console.WriteLine("  interop: managed 64-bit atomics before and after compacting GC");
        AtomicHolder[] counters = new AtomicHolder[16];
        for (int i = 0; i < counters.Length; i++)
            counters[i] = new AtomicHolder { Prefix = i };
        for (int round = 0; round < 2; round++)
        {
            foreach (AtomicHolder counter in counters)
            {
                fixed (long* location = &counter.Separated.Counts.Value)
                    Check(((nuint)location & 7) == 0, "Managed nested int64 field lost eight-byte alignment");
                long beforeValue = Volatile.Read(ref counter.Separated.Counts.Value);
                Check(Interlocked.CompareExchange(ref counter.Separated.Counts.Value, beforeValue + 1, beforeValue) == beforeValue,
                    "Managed nested int64 compare-exchange failed");
                Check(Interlocked.Read(ref counter.Separated.Counts.Value) == round + 1, "Managed int64 read failed");
            }
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        }
        GC.KeepAlive(counters);
    }

    private static void CheckTasks()
    {
        Task<int> work = Task.Run(async () =>
        {
            await Task.Delay(30);
            return 42;
        });
        Check(work.Wait(30000) && work.Result == 42, "Thread pool or timer failed");
    }

    private static void CheckFiles()
    {
        string path = Path.Combine(Path.GetTempPath(), $"legacy-ios-probe-{Guid.NewGuid():N}.txt");
        try
        {
            File.WriteAllText(path, "NativeAOT / iOS 7 / 中文");
            Check(File.ReadAllText(path) == "NativeAOT / iOS 7 / 中文", "File round trip failed");
        }
        finally
        {
            File.Delete(path);
        }
    }
}

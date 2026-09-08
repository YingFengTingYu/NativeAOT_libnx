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

    private sealed class Finalizable
    {
        ~Finalizable() => Interlocked.Increment(ref s_finalized);
    }

    private sealed class Holder
    {
        public int Value;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate double MixedCallback(long value, double a, double b, double c, double d,
        double e, double f, double g, double h);

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

    private static void CheckCallbacks()
    {
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

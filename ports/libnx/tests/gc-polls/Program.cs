using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

internal static unsafe class Program
{
    [DllImport("__Internal", EntryPoint = "gc_poll_record")]
    private static extern void Record(int stage, int value);
    [DllImport("__Internal", EntryPoint = "gc_poll_watchdog_start")]
    private static extern int StartWatchdog(int* release);
    [DllImport("__Internal", EntryPoint = "gc_poll_watchdog_stop")]
    private static extern int StopWatchdog();
    [DllImport("__Internal", EntryPoint = "gc_poll_external_enabled")]
    private static extern int ExternalEnabled();
    [DllImport("__Internal", EntryPoint = "gc_poll_external_count")]
    private static extern ulong ExternalCount();

    private static int finalized;
    private sealed class Finalizable
    {
        ~Finalizable() { Interlocked.Increment(ref finalized); }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void MakeFinalizables()
    {
        for (int index = 0; index < 64; index++) { _ = new Finalizable(); }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static nuint Address(byte[] value)
    {
        fixed (byte* pointer = value) { return (nuint)pointer; }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static byte[] MakeMovable()
    {
        // Adjacent live objects can share the fixed object's pinned plug.
        // Leave an actual dead gap so relocation is a meaningful expectation.
        byte[] gap = new byte[16384];
        byte[] result = new byte[1024];
        Array.Fill(result, (byte)11);
        GC.KeepAlive(gap);
        return result;
    }

    // Every busy path intentionally contains only inlinable operations, with
    // no allocation or native/managed call on which return-address hijacking
    // could depend. Control lives in native memory for the independent watchdog.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Busy(int mode, int* control, byte[] payload)
    {
        uint sum = 0;
        switch (mode)
        {
            case 0:
                Volatile.Write(ref control[1], 1);
                while (Volatile.Read(ref control[0]) == 0) { sum += payload[sum & 1023]; }
                break;
            case 1:
                Volatile.Write(ref control[1], 1);
                while (Volatile.Read(ref control[0]) == 0)
                {
                    while (Volatile.Read(ref control[0]) == 0) { sum += payload[sum & 1023]; }
                }
                break;
            case 2:
                // Two entries into the same strongly connected region.
                Volatile.Write(ref control[1], 1);
                if (Volatile.Read(ref control[2]) == 0) { goto Left; }
                goto Right;
            Left:
                if (Volatile.Read(ref control[0]) != 0) { break; }
                sum += payload[sum & 1023];
                goto Right;
            Right:
                if (Volatile.Read(ref control[0]) != 0) { break; }
                sum += payload[sum & 1023];
                if ((sum & 1) == 0) { goto Left; }
                goto Right;
            case 3:
                try
                {
                    Volatile.Write(ref control[1], 1);
                    while (Volatile.Read(ref control[0]) == 0) { sum += payload[sum & 1023]; }
                }
                finally { Volatile.Write(ref control[3], 1); }
                break;
            case 4:
                int state = Volatile.Read(ref control[2]);
                Volatile.Write(ref control[1], 1);
                while (Volatile.Read(ref control[0]) == 0)
                {
                    switch (state)
                    {
                        case 0: sum += payload[sum & 1023]; state = 2; break;
                        case 1: sum ^= 17; state = 0; break;
                        case 2: sum += payload[(sum + 1) & 1023]; state = 1; break;
                        default: state = 0; break;
                    }
                }
                break;
            case 5:
                try { throw new InvalidOperationException(); }
                catch (InvalidOperationException)
                {
                    Volatile.Write(ref control[1], 1);
                    while (Volatile.Read(ref control[0]) == 0) { sum += payload[sum & 1023]; }
                }
                break;
        }

        int checksum = 0;
        for (int index = 0; index < payload.Length; index++) { checksum += payload[index]; }
        GC.KeepAlive(payload);
        return checksum;
    }

    private static bool RunCase(int mode, int entry)
    {
        int* control = (int*)NativeMemory.AllocZeroed(16);
        byte[] payload = new byte[1024];
        Array.Fill(payload, (byte)7);
        nuint before = Address(payload);
        byte[] movable = MakeMovable();
        nuint movableBefore = Address(movable);
        bool external = ExternalEnabled() != 0;
        int checksum = 0;
        control[2] = entry;
        Thread worker = new(() => checksum = Busy(mode, control, payload));
        worker.Start();
        bool armed = false;
        try
        {
            Stopwatch wait = Stopwatch.StartNew();
            while (Volatile.Read(ref control[1]) == 0)
            {
                if (wait.ElapsedMilliseconds > 2000) { return false; }
                Thread.Sleep(1);
            }
            if (StartWatchdog(control) != 0) { return false; }
            armed = true;
            ulong suspensionsBefore = ExternalCount();
            for (int iteration = 0; iteration < 3; iteration++)
            {
                GC.Collect(2, GCCollectionMode.Forced, true, true);
            }
            int timedOut = StopWatchdog();
            armed = false;
            nuint after = Address(payload);
            bool movableMoved = Address(movable) != movableBefore && movable[511] == 11;
            bool suspensionWorked = external ? ExternalCount() >= suspensionsBefore + 3 && before == after : before != after;
            Record(110 + mode * 2 + entry, (int)(ExternalCount() - suspensionsBefore));
            Record(210 + mode * 2 + entry, before == after ? 1 : 0);
            Record(310 + mode * 2 + entry, movableMoved ? 1 : 0);
            Volatile.Write(ref control[0], 1);
            if (!worker.Join(2000)) { return false; }
            int stage = 10 + mode * 2 + entry;
            Record(stage, timedOut == 0 && checksum == 7168 && (mode != 3 || control[3] == 1) ? 1 : 0);
            Record(stage + 30, suspensionWorked && movableMoved ? 1 : 0);
            Record(stage + 70, timedOut);
            GC.KeepAlive(movable);
            return timedOut == 0 && checksum == 7168 && suspensionWorked && movableMoved && (mode != 3 || control[3] == 1);
        }
        finally
        {
            Volatile.Write(ref control[0], 1);
            if (armed) { StopWatchdog(); }
            if (worker.Join(2000)) { NativeMemory.Free(control); }
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ref byte MakeInterior()
    {
        byte[] payload = new byte[2048];
        Array.Fill(payload, (byte)7);
        return ref payload[37];
    }

    // Only a managed interior byref escapes MakeInterior. No base-object handle
    // in the collector thread can hide a missing register root here.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int BusyInterior(int* control, ref byte first, int readyIndex)
    {
        uint sum = 0;
        Volatile.Write(ref control[readyIndex], 1);
        while (Volatile.Read(ref control[0]) == 0)
        {
            sum += Unsafe.Add(ref first, (int)(sum & 1023));
        }
        int checksum = 0;
        for (int index = 0; index < 1024; index++)
        {
            checksum += Unsafe.Add(ref first, index);
        }
        return checksum;
    }

    private static bool RunInteriorCase(bool multiple)
    {
        int* control = (int*)NativeMemory.AllocZeroed(16);
        int checksum = 0;
        int secondChecksum = 0;
        int nativeWaits = 0;
        Thread worker = new Thread(() => checksum = BusyInterior(control, ref MakeInterior(), 1));
        Thread second = multiple ? new Thread(() => secondChecksum = BusyInterior(control, ref MakeInterior(), 2)) : null;
        Thread nativeWaiter = multiple ? new Thread(() =>
        {
            while (Volatile.Read(ref control[0]) == 0)
            {
                Thread.Sleep(1);
                Interlocked.Increment(ref nativeWaits);
            }
        }) : null;
        worker.Start();
        second?.Start();
        nativeWaiter?.Start();
        bool armed = false;
        try
        {
            Stopwatch wait = Stopwatch.StartNew();
            while (Volatile.Read(ref control[1]) == 0 || (multiple && Volatile.Read(ref control[2]) == 0))
            {
                if (wait.ElapsedMilliseconds > 2000) { return false; }
                Thread.Sleep(1);
            }
            if (StartWatchdog(control) != 0) { return false; }
            armed = true;
            ulong before = ExternalCount();
            for (int iteration = 0; iteration < 32; iteration++)
            {
                GC.Collect(2, GCCollectionMode.Forced, true, true);
            }
            int timedOut = StopWatchdog();
            armed = false;
            Volatile.Write(ref control[0], 1);
            bool passed = worker.Join(2000) && checksum == 7168 && timedOut == 0 &&
                ExternalCount() >= before + (multiple ? 64UL : 32UL);
            if (multiple)
            {
                passed &= second.Join(2000) && nativeWaiter.Join(2000) && secondChecksum == 7168 && nativeWaits > 0;
            }
            Record(multiple ? 73 : 72, passed ? 1 : 0);
            return passed;
        }
        finally
        {
            Volatile.Write(ref control[0], 1);
            if (armed) { StopWatchdog(); }
            bool stopped = worker.Join(2000);
            if (multiple) { stopped &= second.Join(2000) & nativeWaiter.Join(2000); }
            if (stopped) { NativeMemory.Free(control); }
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "managed_gc_poll_main")]
    private static int Main()
    {
        try
        {
            Record(1, ExternalEnabled());
            for (int mode = 0; mode < 6; mode++)
            {
                if (!RunCase(mode, 0)) { return 10 + mode; }
                if (mode == 2 && !RunCase(mode, 1)) { return 20; }
            }
            if (ExternalEnabled() != 0)
            {
                if (!RunInteriorCase(false)) { return 72; }
                if (!RunInteriorCase(true)) { return 73; }
            }
            MakeFinalizables();
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Record(70, finalized);
            if (finalized != 64) { return 70; }
            int finished = 0;
            for (int index = 0; index < 64; index++)
            {
                Thread worker = new(() => Interlocked.Increment(ref finished));
                worker.Start();
                if (!worker.Join(2000)) { return 71; }
            }
            Record(71, finished);
            return finished == 64 ? 0 : 71;
        }
        catch { Record(99, -1); return 99; }
    }
}

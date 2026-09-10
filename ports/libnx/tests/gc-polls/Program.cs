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
            for (int iteration = 0; iteration < 3; iteration++)
            {
                GC.Collect(2, GCCollectionMode.Forced, true, true);
            }
            int timedOut = StopWatchdog();
            armed = false;
            nuint after = Address(payload);
            Volatile.Write(ref control[0], 1);
            if (!worker.Join(2000)) { return false; }
            int stage = 10 + mode * 2 + entry;
            Record(stage, timedOut == 0 && checksum == 7168 && (mode != 3 || control[3] == 1) ? 1 : 0);
            Record(stage + 30, before != after ? 1 : 0);
            Record(stage + 70, timedOut);
            return timedOut == 0 && checksum == 7168 && before != after && (mode != 3 || control[3] == 1);
        }
        finally
        {
            Volatile.Write(ref control[0], 1);
            if (armed) { StopWatchdog(); }
            if (worker.Join(2000)) { NativeMemory.Free(control); }
        }
    }

    [UnmanagedCallersOnly(EntryPoint = "managed_gc_poll_main")]
    private static int Main()
    {
        try
        {
            for (int mode = 0; mode < 6; mode++)
            {
                if (!RunCase(mode, 0)) { return 10 + mode; }
                if (mode == 2 && !RunCase(mode, 1)) { return 20; }
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
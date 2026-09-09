// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
#if LEGACY_IOS_ARM32
using CGFloat = System.Single;
#else
using CGFloat = System.Double;
#endif

internal static unsafe class Program
{
    private sealed class Model { internal int Taps, ManualTaps, Collections, Active, Background; }
    private static readonly Model State = new Model();
    private static nint s_delegate, s_window, s_controller, s_countLabel, s_statusLabel, s_lifecycleLabel, s_button;
    private static string s_directory = "";
    private static string s_error = "";
    private static bool s_automatic, s_smokePassed, s_workerPassed;
    private static string Architecture => IntPtr.Size == 4 ? "ARM32" : "ARM64";

    private static int Main()
    {
        nint pool = ObjC.New("NSAutoreleasePool");
        try
        {
            s_directory = Path.Combine(ObjC.ManagedString(ObjC.NSHomeDirectory()), "Documents", "NativeAOTUIKit" + (IntPtr.Size == 4 ? "32" : "64"));
            Directory.CreateDirectory(s_directory);
            Log("main", "pid=" + Environment.ProcessId);
            if (!ObjC.IsMainThread)
                throw new InvalidOperationException("UIApplicationMain must run on the OS main thread");
            nint cls = ObjC.objc_allocateClassPair(ObjC.Class("NSObject"), "LegacyNativeAOTAppDelegate", 0);
            if (cls == 0)
                throw new InvalidOperationException("Unable to create app delegate class");
            Add(cls, "application:didFinishLaunchingWithOptions:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint, byte>)&Launched,
                IntPtr.Size == 4 ? "c@:@@" : "B@:@@");
            Add(cls, "applicationDidBecomeActive:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&BecameActive, "v@:@");
            Add(cls, "applicationWillResignActive:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&ResignedActive, "v@:@");
            Add(cls, "applicationDidEnterBackground:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&EnteredBackground, "v@:@");
            Add(cls, "applicationWillEnterForeground:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&EnteringForeground, "v@:@");
            Add(cls, "application:openURL:sourceApplication:annotation:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint, nint, nint, byte>)&OpenUrl,
                IntPtr.Size == 4 ? "c@:@@@@" : "B@:@@@@");
            Add(cls, "tap:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&Tapped, "v@:@");
            Add(cls, "collect:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&Collect, "v@:@");
            Add(cls, "smoke:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&Smoke, "v@:@");
            Add(cls, "workerReturned:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&WorkerReturned, "v@:@");
            Add(cls, "snapshot:", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, void>)&Snapshot, "v@:@");
            Add(cls, "window", (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint>)&Window, "@@:");
            nint protocol = ObjC.objc_getProtocol("UIApplicationDelegate");
            if (protocol != 0)
                ObjC.class_addProtocol(cls, protocol);
            ObjC.objc_registerClassPair(cls);
            return ObjC.UIApplicationMain(0, 0, 0, ObjC.String("LegacyNativeAOTAppDelegate"));
        }
        catch (Exception error)
        {
            Fail(error);
            return 1;
        }
        finally
        {
            ObjC.Send(pool, ObjC.Sel("drain"));
        }
    }

    private static void Add(nint cls, string name, nint callback, string signature)
    {
        if (ObjC.class_addMethod(cls, ObjC.Sel(name), callback, signature) == 0)
            throw new InvalidOperationException("Unable to register " + name);
    }

    private static void Log(string name, string detail = "")
    {
        string line = $"{DateTime.UtcNow:O} {Architecture} {name} main={ObjC.IsMainThread} {detail}";
        Console.WriteLine(line);
        if (s_directory.Length != 0)
            File.AppendAllText(Path.Combine(s_directory, "events.log"), line + "\n");
    }

    private static void Fail(Exception error)
    {
        // Objective-C callers must never receive a managed exception.
        s_error = error.ToString();
        try { Log("FAIL", s_error); SaveStatus(); } catch { }
    }

    private static void SaveStatus()
    {
        File.WriteAllText(Path.Combine(s_directory, "status.txt"),
            $"architecture={Architecture}\npid={Environment.ProcessId}\nmain={ObjC.IsMainThread}\nsmoke={s_smokePassed}\nworker={s_workerPassed}\n" +
            $"taps={State.Taps}\nmanual_taps={State.ManualTaps}\ngc={State.Collections}\nactive={State.Active}\nbackground={State.Background}\nerror={s_error}\n");
    }

    private static nint Label(nint parent, string text, double y, double height, double fontSize, nint color)
    {
        ObjC.Rect bounds = ObjC.GetRect(parent, ObjC.Sel("bounds"));
        nint label = ObjC.New("UILabel");
        ObjC.SendRect(label, ObjC.Sel("setFrame:"), new ObjC.Rect(32, y, bounds.Width - 64, height));
        ObjC.SendInteger(label, ObjC.Sel("setAutoresizingMask:"), 2);
        ObjC.SendInteger(label, ObjC.Sel("setTextAlignment:"), 1);
        ObjC.SendInteger(label, ObjC.Sel("setNumberOfLines:"), 0);
        ObjC.SendObject(label, ObjC.Sel("setTextColor:"), color);
        ObjC.SendObject(label, ObjC.Sel("setBackgroundColor:"), ObjC.Send(ObjC.Class("UIColor"), ObjC.Sel("clearColor")));
        ObjC.SendObject(label, ObjC.Sel("setFont:"), ObjC.SendFloat(ObjC.Class("UIFont"), ObjC.Sel("systemFontOfSize:"), (CGFloat)fontSize));
        ObjC.SetText(label, text);
        ObjC.SendObject(parent, ObjC.Sel("addSubview:"), label);
        ObjC.Send(label, ObjC.Sel("release")); // The view hierarchy owns it now.
        return label;
    }

    private static nint Button(nint parent, string title, string action, double y, nint color)
    {
        ObjC.Rect bounds = ObjC.GetRect(parent, ObjC.Sel("bounds"));
        nint button = ObjC.SendInteger(ObjC.Class("UIButton"), ObjC.Sel("buttonWithType:"), 1);
        ObjC.SendRect(button, ObjC.Sel("setFrame:"), new ObjC.Rect(40, y, bounds.Width - 80, 56));
        ObjC.SendInteger(button, ObjC.Sel("setAutoresizingMask:"), 2);
        ObjC.SendObject(button, ObjC.Sel("setBackgroundColor:"), color);
        ObjC.SetTitle(button, ObjC.Sel("setTitle:forState:"), ObjC.String(title), 0);
        ObjC.SetTitle(button, ObjC.Sel("setTitleColor:forState:"), ObjC.Send(ObjC.Class("UIColor"), ObjC.Sel("whiteColor")), 0);
        ObjC.AddTarget(button, ObjC.Sel("addTarget:action:forControlEvents:"), s_delegate, ObjC.Sel(action), 1 << 6);
        ObjC.SendObject(parent, ObjC.Sel("addSubview:"), button);
        return button;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte Launched(nint self, nint command, nint app, nint options)
    {
        try
        {
            s_delegate = self;
            Log("launched");
            ObjC.Rect bounds = ObjC.GetRect(ObjC.Send(ObjC.Class("UIScreen"), ObjC.Sel("mainScreen")), ObjC.Sel("bounds"));
            if (bounds.Width < 100 || bounds.Height < 100)
                throw new InvalidOperationException("UIScreen bounds ABI mismatch");
            s_window = ObjC.SendRect(ObjC.Send(ObjC.Class("UIWindow"), ObjC.Sel("alloc")), ObjC.Sel("initWithFrame:"), bounds);
            s_controller = ObjC.New("UIViewController");
            ObjC.SendObject(s_window, ObjC.Sel("setRootViewController:"), s_controller);
            nint view = ObjC.Send(s_controller, ObjC.Sel("view"));
            ObjC.SendRect(view, ObjC.Sel("setFrame:"), bounds);
            ObjC.SendObject(view, ObjC.Sel("setBackgroundColor:"), ObjC.Color(0.055, 0.09, 0.16));
            nint white = ObjC.Send(ObjC.Class("UIColor"), ObjC.Sel("whiteColor"));
            nint muted = ObjC.Color(0.67, 0.75, 0.86);
            string os = ObjC.ManagedString(ObjC.Send(ObjC.Send(ObjC.Class("UIDevice"), ObjC.Sel("currentDevice")), ObjC.Sel("systemVersion")));
            Label(view, "NativeAOT", 58, 60, 36, white);
            Label(view, Architecture + " · iOS " + os, 120, 32, 17, muted);
            s_countLabel = Label(view, "0", 175, 82, 64, white);
            Label(view, "点击次数（含自动检查）", 254, 32, 15, muted);
            s_button = Button(view, "点击 +1", "tap:", 308, ObjC.Color(0.13, 0.43, 0.91));
            Button(view, "回收内存 / GC", "collect:", 382, ObjC.Color(0.20, 0.29, 0.43));
            s_statusLabel = Label(view, "正在检查主线程和回调…", 460, 64, 16, white);
            s_lifecycleLabel = Label(view, "", 532, 64, 14, muted);
            ObjC.Send(s_window, ObjC.Sel("makeKeyAndVisible"));
            Update();
            ObjC.Schedule(self, ObjC.Sel("performSelector:withObject:afterDelay:"), ObjC.Sel("smoke:"), 0, 0.5);
            return 1;
        }
        catch (Exception error) { Fail(error); return 0; }
    }

    private static void Update()
    {
        if (!ObjC.IsMainThread)
            throw new InvalidOperationException("UI callback is not on the main thread");
        ObjC.SetText(s_countLabel, State.Taps.ToString());
        ObjC.SetText(s_statusLabel, s_error.Length != 0 ? "检查失败，请查看日志" :
            s_smokePassed && s_workerPassed ? "自动检查通过 · 主线程正常\nGC 后回调仍然有效" : "正在检查主线程和回调…");
        ObjC.SetText(s_lifecycleLabel, $"手动点击 {State.ManualTaps} 次 · GC {State.Collections} 次\n进入前台 {State.Active} 次 · 进入后台 {State.Background} 次");
        SaveStatus();
    }

    private static void ScheduleSnapshot()
    {
        ObjC.CancelScheduled(ObjC.Class("NSObject"), ObjC.Sel("cancelPreviousPerformRequestsWithTarget:selector:object:"), s_delegate, ObjC.Sel("snapshot:"), 0);
        ObjC.Schedule(s_delegate, ObjC.Sel("performSelector:withObject:afterDelay:"), ObjC.Sel("snapshot:"), 0, 0.2);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint Window(nint self, nint command) => s_window;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static byte OpenUrl(nint self, nint command, nint app, nint url, nint source, nint annotation)
    {
        try { Log("open-url"); Update(); ScheduleSnapshot(); return 1; }
        catch (Exception error) { Fail(error); return 0; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Tapped(nint self, nint command, nint sender)
    {
        try { State.Taps++; if (!s_automatic) State.ManualTaps++; Log("tap", "automatic=" + s_automatic); Update(); ScheduleSnapshot(); }
        catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Collect(nint self, nint command, nint sender)
    {
        try { GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true); GC.WaitForPendingFinalizers(); State.Collections++; Log("gc"); Update(); ScheduleSnapshot(); }
        catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void BecameActive(nint self, nint command, nint app)
    {
        try { State.Active++; Log("active"); Update(); ScheduleSnapshot(); } catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ResignedActive(nint self, nint command, nint app)
    {
        try { Log("resign-active"); SaveStatus(); } catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void EnteredBackground(nint self, nint command, nint app)
    {
        try { State.Background++; Log("background"); SaveStatus(); } catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void EnteringForeground(nint self, nint command, nint app)
    {
        try { Log("foreground"); Update(); } catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Smoke(nint self, nint command, nint argument)
    {
        try
        {
            int before = State.Taps;
            s_automatic = true;
            ObjC.SendInteger(s_button, ObjC.Sel("sendActionsForControlEvents:"), 1 << 6);
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            State.Collections++;
            ObjC.SendInteger(s_button, ObjC.Sel("sendActionsForControlEvents:"), 1 << 6);
            s_smokePassed = State.Taps == before + 2;
            if (!s_smokePassed)
                throw new InvalidOperationException("UIControl callback/GC check failed");
            Log("smoke-pass");
            Update();
            new Thread(() =>
            {
                nint pool = ObjC.New("NSAutoreleasePool");
                try
                {
                    if (ObjC.IsMainThread) throw new InvalidOperationException("Worker ran on main thread");
                    ObjC.OnMainThread(s_delegate, ObjC.Sel("performSelectorOnMainThread:withObject:waitUntilDone:"), ObjC.Sel("workerReturned:"), 0, 0);
                }
                catch (Exception error) { Fail(error); }
                finally { ObjC.Send(pool, ObjC.Sel("drain")); }
            }) { IsBackground = true }.Start();
        }
        catch (Exception error) { Fail(error); }
        finally { s_automatic = false; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void WorkerReturned(nint self, nint command, nint argument)
    {
        try { s_workerPassed = ObjC.IsMainThread; Log("worker-returned"); Update(); ScheduleSnapshot(); }
        catch (Exception error) { Fail(error); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Snapshot(nint self, nint command, nint argument)
    {
        try
        {
            ObjC.Rect bounds = ObjC.GetRect(s_window, ObjC.Sel("bounds"));
            ObjC.UIGraphicsBeginImageContextWithOptions(new ObjC.Size(bounds.Width, bounds.Height), 1, 1);
            try
            {
                if (ObjC.DrawHierarchy(s_window, ObjC.Sel("drawViewHierarchyInRect:afterScreenUpdates:"), bounds, 1) == 0)
                    throw new InvalidOperationException("Window snapshot failed");
                nint png = ObjC.UIImagePNGRepresentation(ObjC.UIGraphicsGetImageFromCurrentImageContext());
                if (ObjC.WriteFile(png, ObjC.Sel("writeToFile:atomically:"), ObjC.String(Path.Combine(s_directory, "latest.png")), 1) == 0)
                    throw new InvalidOperationException("Unable to save window snapshot");
            }
            finally { ObjC.UIGraphicsEndImageContext(); }
            Log("snapshot");
        }
        catch (Exception error) { Fail(error); }
    }
}

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Runtime.InteropServices;
#if LEGACY_IOS_ARM32
using CGFloat = System.Single;
#else
using CGFloat = System.Double;
#endif

internal static class ObjC
{
    private const string Library = "/usr/lib/libobjc.A.dylib";
    private const string UIKit = "/System/Library/Frameworks/UIKit.framework/UIKit";
    private const string Foundation = "/System/Library/Frameworks/Foundation.framework/Foundation";

    [StructLayout(LayoutKind.Sequential)]
    internal struct Rect
    {
        internal CGFloat X, Y, Width, Height;
        internal Rect(double x, double y, double width, double height)
        {
            X = (CGFloat)x; Y = (CGFloat)y; Width = (CGFloat)width; Height = (CGFloat)height;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Size
    {
        internal CGFloat Width, Height;
        internal Size(CGFloat width, CGFloat height) { Width = width; Height = height; }
    }

    [DllImport(Library, EntryPoint = "objc_getClass")]
    internal static extern nint Class([MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Library, EntryPoint = "sel_registerName")]
    internal static extern nint Sel([MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Library)]
    internal static extern nint objc_allocateClassPair(nint superclass, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, nuint extra);
    [DllImport(Library)]
    internal static extern void objc_registerClassPair(nint cls);
    [DllImport(Library)]
    internal static extern byte class_addMethod(nint cls, nint selector, nint implementation, [MarshalAs(UnmanagedType.LPUTF8Str)] string types);
    [DllImport(Library)]
    internal static extern nint objc_getProtocol([MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Library)]
    internal static extern byte class_addProtocol(nint cls, nint protocol);

    // Each objc_msgSend import has the actual method signature. A C variadic
    // declaration would pass floating-point arguments incorrectly on ARM64.
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint Send(nint receiver, nint selector);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendObject(nint receiver, nint selector, nint value);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendInteger(nint receiver, nint selector, nint value);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendFloat(nint receiver, nint selector, CGFloat value);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendColor(nint receiver, nint selector, CGFloat r, CGFloat g, CGFloat b, CGFloat a);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendString(nint receiver, nint selector, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern nint SendRect(nint receiver, nint selector, Rect value);
#if LEGACY_IOS_ARM32
    [DllImport(Library, EntryPoint = "objc_msgSend_stret")]
#else
    [DllImport(Library, EntryPoint = "objc_msgSend")]
#endif
    internal static extern Rect GetRect(nint receiver, nint selector);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern byte GetBool(nint receiver, nint selector);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void SetBool(nint receiver, nint selector, byte value);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void SetTitle(nint receiver, nint selector, nint text, nuint state);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void AddTarget(nint receiver, nint selector, nint target, nint action, nuint events);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void Schedule(nint receiver, nint selector, nint action, nint argument, double delay);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void CancelScheduled(nint receiver, nint selector, nint target, nint action, nint argument);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern void OnMainThread(nint receiver, nint selector, nint action, nint argument, byte wait);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern byte DrawHierarchy(nint receiver, nint selector, Rect rect, byte afterUpdates);
    [DllImport(Library, EntryPoint = "objc_msgSend")]
    internal static extern byte WriteFile(nint receiver, nint selector, nint path, byte atomic);

    [DllImport(UIKit)]
    internal static extern int UIApplicationMain(int argc, nint argv, nint principalClass, nint delegateClass);
    [DllImport(UIKit)]
    internal static extern void UIGraphicsBeginImageContextWithOptions(Size size, byte opaque, CGFloat scale);
    [DllImport(UIKit)]
    internal static extern nint UIGraphicsGetImageFromCurrentImageContext();
    [DllImport(UIKit)]
    internal static extern void UIGraphicsEndImageContext();
    [DllImport(UIKit)]
    internal static extern nint UIImagePNGRepresentation(nint image);
    [DllImport(Foundation)]
    internal static extern nint NSHomeDirectory();

    internal static nint String(string value) => SendString(Class("NSString"), Sel("stringWithUTF8String:"), value);
    internal static string ManagedString(nint value) => Marshal.PtrToStringUTF8(Send(value, Sel("UTF8String"))) ?? "";
    internal static nint New(string name) => Send(Send(Class(name), Sel("alloc")), Sel("init"));
    internal static void SetText(nint label, string text) => SendObject(label, Sel("setText:"), String(text));
    internal static bool IsMainThread => GetBool(Class("NSThread"), Sel("isMainThread")) != 0;
    internal static nint Color(double r, double g, double b) => SendColor(Class("UIColor"), Sel("colorWithRed:green:blue:alpha:"), (CGFloat)r, (CGFloat)g, (CGFloat)b, 1);
}

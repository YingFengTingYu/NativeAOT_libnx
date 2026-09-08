// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
using System.Net;
using System.Net.Sockets;
using System.Net.NetworkInformation;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

internal static class Program
{
    [DllImport("__Internal", EntryPoint = "network_record", CharSet = CharSet.Ansi)]
    private static extern void Record(string message);
    [DllImport("__Internal", EntryPoint = "SystemNative_GetNetworkInterfaces")]
    private static extern int GetInterfaces(out int interfaceCount, out IntPtr interfaces, out int addressCount, out IntPtr addresses);
    [DllImport("__Internal", EntryPoint = "SystemNative_Free")]
    private static extern void Free(IntPtr memory);

    [UnmanagedCallersOnly(EntryPoint = "managed_network_main")]
    public static int Main()
    {
        try
        {
            Run().GetAwaiter().GetResult();
            Record("pass=1");
            return 0;
        }
        catch (Exception exception)
        {
            Record(exception.ToString());
            Record("pass=0");
            return 1;
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static async Task Run()
    {
        IPAddress[] addresses = await Dns.GetHostAddressesAsync("example.com", AddressFamily.InterNetwork);
        Require(addresses.Length > 0, "DNS returned no addresses");
        Record("dns=1");
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(40));
        CancellationToken token = deadline.Token;
        byte[] expected = Encoding.UTF8.GetBytes("libnx-托管-network");
        using TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        using TcpClient client = new(AddressFamily.InterNetwork);
        ValueTask<TcpClient> accepting = listener.AcceptTcpClientAsync(token);
        await client.ConnectAsync(IPAddress.Loopback, port, token);
        using TcpClient server = await accepting;
        using NetworkStream incoming = server.GetStream();
        using NetworkStream outgoing = client.GetStream();
        byte[] data = new byte[expected.Length];
        for (int iteration = 0; iteration < 64; iteration++)
        {
            ValueTask read = incoming.ReadExactlyAsync(data, token);
            await Task.Delay(2, token);
            await outgoing.WriteAsync(expected, token);
            await read;
            Require(data.AsSpan().SequenceEqual(expected), "TCP mismatch");
        }
        Record("tcp.async_rearm=64");
        using (CancellationTokenSource cancel = new(TimeSpan.FromMilliseconds(100)))
        {
            try { await incoming.ReadAsync(data, cancel.Token); throw new InvalidOperationException("receive did not cancel"); }
            catch (OperationCanceledException) { Record("receive.cancel=1"); }
        }
        await outgoing.WriteAsync(expected, token);
        await incoming.ReadExactlyAsync(data, token);
        Require(data.AsSpan().SequenceEqual(expected), "receive after cancel failed");
        Record("receive.after_cancel=1");
        using (Socket closing = new(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp))
        {
            closing.Bind(new IPEndPoint(IPAddress.Loopback, 0));
            ValueTask<SocketReceiveFromResult> pending = closing.ReceiveFromAsync(data, SocketFlags.None, new IPEndPoint(IPAddress.Any, 0), token);
            closing.Dispose();
            try { await pending; throw new InvalidOperationException("disposed receive succeeded"); }
            catch (Exception exception) when (exception is SocketException or ObjectDisposedException or OperationCanceledException) { Record("receive.dispose=1"); }
        }
        using (UdpClient udp = new(new IPEndPoint(IPAddress.Loopback, 0)))
        {
            IPEndPoint endpoint = (IPEndPoint)udp.Client.LocalEndPoint!;
            for (int iteration = 0; iteration < 16; iteration++)
            {
                ValueTask<UdpReceiveResult> receive = udp.ReceiveAsync(token);
                await udp.SendAsync(expected, endpoint, token);
                UdpReceiveResult result = await receive;
                Require(result.Buffer.AsSpan().SequenceEqual(expected), "UDP mismatch");
            }
            Record("udp.async=16");
            try
            {
                Task<SocketReceiveMessageFromResult> message = udp.Client.ReceiveMessageFromAsync(data, SocketFlags.None,
                    new IPEndPoint(IPAddress.Any, 0), token).AsTask();
                await udp.SendAsync(expected, endpoint, token);
                await message;
                Record("udp.packet_info=1");
            }
            catch (SocketException exception) when (exception.SocketErrorCode == SocketError.OperationNotSupported ||
                exception.SocketErrorCode == SocketError.ProtocolOption || exception.SocketErrorCode == SocketError.ProtocolNotSupported)
            {
                UdpReceiveResult fallback = await udp.ReceiveAsync(token);
                Require(fallback.Buffer.AsSpan().SequenceEqual(expected), "UDP fallback lost queued payload");
                Record("udp.packet_info_fallback=1");
            }
        }
        await HttpAndWebSocket(token);
        try { await Multicast(token); }
        catch (SocketException exception) { Record($"udp.multicast_unverified={exception.SocketErrorCode}"); }
        CheckInterfaces();
        try
        {
            NetworkInterface.GetAllNetworkInterfaces();
            Record("bcl_interfaces.available=1");
        }
        catch (DirectoryNotFoundException) { Record("bcl_interfaces.requires_platform_override=1"); }
        using TcpListener refusedListener = new(IPAddress.Loopback, 0);
        refusedListener.Start();
        int refusedPort = ((IPEndPoint)refusedListener.LocalEndpoint).Port;
        refusedListener.Stop();
        using Socket refused = new(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
        try { await refused.ConnectAsync(IPAddress.Loopback, refusedPort, token); throw new InvalidOperationException("unexpected connect success"); }
        catch (SocketException exception) { Require(exception.SocketErrorCode == SocketError.ConnectionRefused, exception.ToString()); Record("connect.refused=1"); }
    }

    private static unsafe void CheckInterfaces()
    {
        int result = GetInterfaces(out int count, out IntPtr interfaces, out int addressCount, out IntPtr addresses);
        Require(result == 0 && count == 1 && addressCount == 1, "No active nifm IPv4 interface");
        try
        {
            byte* address = (byte*)addresses;
            Require(address[20] == 4 && address[21] <= 32, "Invalid nifm address");
            Record($"interface.address={new IPAddress(new ReadOnlySpan<byte>(address + 4, 4))}/{address[21]}");
            Record("interfaces.native=1");
        }
        finally { Free(interfaces); }
    }

    private static async Task Multicast(CancellationToken token)
    {
        IPAddress group = IPAddress.Parse("239.255.80.90");
        using UdpClient receiver = new(new IPEndPoint(IPAddress.Any, 0));
        receiver.JoinMulticastGroup(group, IPAddress.Loopback);
        using UdpClient sender = new(new IPEndPoint(IPAddress.Loopback, 0));
        sender.Client.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.MulticastInterface, IPAddress.Loopback.GetAddressBytes());
        sender.MulticastLoopback = true;
        sender.Ttl = 1;
        sender.EnableBroadcast = true;
        byte[] message = Encoding.ASCII.GetBytes("libnx-lan-discovery");
        ValueTask<UdpReceiveResult> receive = receiver.ReceiveAsync(token);
        await sender.SendAsync(message, new IPEndPoint(group, ((IPEndPoint)receiver.Client.LocalEndPoint!).Port), token);
        UdpReceiveResult response = await receive;
        Require(response.Buffer.AsSpan().SequenceEqual(message), "Multicast mismatch");
        receiver.DropMulticastGroup(group);
        Record("udp.multicast=1");
    }

    private static async Task<string> ReadHeaders(NetworkStream stream, CancellationToken token)
    {
        List<byte> bytes = [];
        byte[] single = new byte[1];
        while (bytes.Count < 16384)
        {
            await stream.ReadExactlyAsync(single, token);
            bytes.Add(single[0]);
            int size = bytes.Count;
            if (size >= 4 && bytes[size - 4] == 13 && bytes[size - 3] == 10 && bytes[size - 2] == 13 && bytes[size - 1] == 10)
                return Encoding.ASCII.GetString(bytes.ToArray());
        }
        throw new InvalidOperationException("Headers too long");
    }

    private static async Task HttpAndWebSocket(CancellationToken token)
    {
        using TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task serveHttp = Task.Run(async () =>
        {
            using TcpClient client = await listener.AcceptTcpClientAsync(token);
            using NetworkStream stream = client.GetStream();
            string request = await ReadHeaders(stream, token);
            Require(request.StartsWith("GET /probe "), "Wrong HTTP request");
            await stream.WriteAsync(Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nlibnx"), token);
        }, token);
        using SocketsHttpHandler handler = new() { UseProxy = false, AutomaticDecompression = DecompressionMethods.None };
        using HttpClient http = new(handler);
        string body = await http.GetStringAsync($"http://127.0.0.1:{port}/probe", token);
        Require(body == "libnx", "HTTP response mismatch");
        await serveHttp;
        Record("http.get=1");
        Task serveWebSocket = Task.Run(async () =>
        {
            using TcpClient client = await listener.AcceptTcpClientAsync(token);
            using NetworkStream stream = client.GetStream();
            string request = await ReadHeaders(stream, token);
            string key = request.Split("\r\n").Single(line => line.StartsWith("Sec-WebSocket-Key:", StringComparison.OrdinalIgnoreCase)).Split(':', 2)[1].Trim();
            string accept = Convert.ToBase64String(SHA1.HashData(Encoding.ASCII.GetBytes(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
            await stream.WriteAsync(Encoding.ASCII.GetBytes($"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n"), token);
            using WebSocket socket = WebSocket.CreateFromStream(stream, true, null, Timeout.InfiniteTimeSpan);
            byte[] data = new byte[128];
            WebSocketReceiveResult result = await socket.ReceiveAsync(new ArraySegment<byte>(data), token);
            await socket.SendAsync(new ArraySegment<byte>(data, 0, result.Count), result.MessageType, result.EndOfMessage, token);
            await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "done", token);
        }, token);
        using ClientWebSocket websocket = new();
        websocket.Options.Proxy = null;
        await websocket.ConnectAsync(new Uri($"ws://127.0.0.1:{port}/socket"), token);
        byte[] payload = Encoding.UTF8.GetBytes("WebSocket中文往返");
        await websocket.SendAsync(payload.AsMemory(), WebSocketMessageType.Text, true, token);
        byte[] answer = new byte[128];
        ValueWebSocketReceiveResult reply = await websocket.ReceiveAsync(answer.AsMemory(), token);
        Require(reply.EndOfMessage && answer.AsSpan(0, reply.Count).SequenceEqual(payload), "WebSocket mismatch");
        await websocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "done", token);
        await serveWebSocket;
        Record("websocket.roundtrip_close=1");
    }
}

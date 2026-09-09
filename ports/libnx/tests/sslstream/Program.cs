// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading.Channels;

internal static class Program
{
#if LIBNX_PROBE
    [DllImport("__Internal", EntryPoint = "tls_record", CharSet = CharSet.Ansi)]
    private static extern void Record(string message);
    [UnmanagedCallersOnly(EntryPoint = "managed_tls_main")]
#else
    private static void Record(string message) => Console.WriteLine("[AOTSSL] " + message);
#endif
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
        Record("begin=1");
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(100));
        CancellationToken token = deadline.Token;
        Require(Convert.ToHexString(SHA256.HashData("abc"u8)) ==
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "SHA256 regression");
        using ECDsa key = ECDsa.Create(ECCurve.NamedCurves.nistP256);
        CertificateRequest request = new("CN=localhost", key, HashAlgorithmName.SHA256);
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(true, false, 0, true));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(
            X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyCertSign, true));
        SubjectAlternativeNameBuilder names = new();
        names.AddDnsName("localhost");
        request.CertificateExtensions.Add(names.Build());
        using X509Certificate2 generated = request.CreateSelfSigned(
            DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        // Re-import also exercises PKCS#12. Windows Schannel cannot use all
        // ephemeral ECDSA keys directly; normal key-set lifetime is disposed here.
        using X509Certificate2 certificate = X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pkcs12), null);
        Record("crypto.certificate=1");
        await MemoryTls(certificate, SslProtocols.Tls12, token);
        await MemoryTls(certificate, SslProtocols.Tls13, token);
        await RejectCertificate(certificate, "wrong.invalid", true, SslPolicyErrors.RemoteCertificateNameMismatch, token);
        await RejectCertificate(certificate, "localhost", false, SslPolicyErrors.RemoteCertificateChainErrors, token);
        await CancelHandshake(token);
        await HttpsAndWebSocket(certificate, token);
#if LIBNX_PROBE
        await PublicHttps(token);
#endif
    }

    private static X509ChainPolicy Trust(X509Certificate2 certificate)
    {
        X509ChainPolicy policy = new()
        {
            TrustMode = X509ChainTrustMode.CustomRootTrust,
            RevocationMode = X509RevocationMode.NoCheck,
            DisableCertificateDownloads = true
        };
        policy.CustomTrustStore.Add(certificate);
        return policy;
    }

    private static async Task MemoryTls(X509Certificate2 certificate, SslProtocols protocol, CancellationToken token)
    {
        (PipeStream left, PipeStream right) = PipeStream.CreatePair();
        Record("memory." + protocol + ".begin=1");
        using SslStream client = new(left, leaveInnerStreamOpen: true);
        using SslStream server = new(right, leaveInnerStreamOpen: true);
        await Task.WhenAll(
            CloseOnFailure(server.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate, EnabledSslProtocols = protocol,
                ApplicationProtocols = [SslApplicationProtocol.Http11]
            }, token), right),
            CloseOnFailure(client.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = "localhost", EnabledSslProtocols = protocol,
                CertificateChainPolicy = Trust(certificate),
                ApplicationProtocols = [SslApplicationProtocol.Http11]
            }, token), left));
        Record("memory." + protocol + ".authenticated=1");
        Require(client.IsAuthenticated && server.IsAuthenticated && client.IsEncrypted, "TLS authentication");
        Require(client.SslProtocol == protocol && server.SslProtocol == protocol, "Protocol selection");
        Require(client.NegotiatedApplicationProtocol == SslApplicationProtocol.Http11, "ALPN");
        byte[] expected = RandomNumberGenerator.GetBytes(65537);
        byte[] received = new byte[expected.Length];
        await Task.WhenAll(client.WriteAsync(expected, token).AsTask(), server.ReadExactlyAsync(received, token).AsTask());
        Require(expected.AsSpan().SequenceEqual(received), "Fragmented arbitrary Stream payload");
        using (CancellationTokenSource cancel = CancellationTokenSource.CreateLinkedTokenSource(token))
        {
            Task<int> pending = client.ReadAsync(new byte[8], cancel.Token).AsTask();
            cancel.Cancel();
            try { await pending; throw new InvalidOperationException("Read ignored cancellation"); }
            catch (OperationCanceledException) when (cancel.IsCancellationRequested) { }
        }
        await server.WriteAsync("after cancel"u8.ToArray(), token);
        byte[] response = new byte[12];
        await client.ReadExactlyAsync(response, token);
        Require(response.AsSpan().SequenceEqual("after cancel"u8), "Read after cancellation");
        await server.ShutdownAsync();
        Require(await client.ReadAsync(response, token) == 0, "close_notify EOF");
        client.Dispose();
        Require(!left.IsDisposed, "leaveInnerStreamOpen");
        left.Dispose();
        right.Dispose();
        Record("memory." + protocol + ".fragment_alpn_cancel_close=1");
    }

    private static async Task CloseOnFailure(Task operation, Stream stream)
    {
        try { await operation; }
        catch { stream.Dispose(); throw; }
    }

    private static async Task ObserveServer(Task operation)
    {
        try { await operation; }
        catch (Exception exception) { Record("server.error=" + exception); throw; }
    }

    private static async Task RejectCertificate(X509Certificate2 certificate, string host, bool trust,
        SslPolicyErrors expectedErrors, CancellationToken token)
    {
        (PipeStream left, PipeStream right) = PipeStream.CreatePair();
        using SslStream client = new(left);
        using SslStream server = new(right);
        SslPolicyErrors observed = SslPolicyErrors.None;
        Task serving = server.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
        {
            ServerCertificate = certificate, EnabledSslProtocols = SslProtocols.Tls12
        }, token);
        try
        {
            await client.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = host, EnabledSslProtocols = SslProtocols.Tls12,
                CertificateChainPolicy = trust ? Trust(certificate) : new X509ChainPolicy
                {
                    TrustMode = X509ChainTrustMode.CustomRootTrust, RevocationMode = X509RevocationMode.NoCheck,
                    DisableCertificateDownloads = true
                },
                RemoteCertificateValidationCallback = (_, _, _, errors) =>
                {
                    observed = errors;
                    return errors == SslPolicyErrors.None;
                }
            }, token);
            throw new InvalidOperationException("Invalid certificate was accepted");
        }
        catch (AuthenticationException)
        {
            Require((observed & expectedErrors) == expectedErrors, "Certificate policy callback lost error flags");
        }
        finally
        {
            client.Dispose();
            try { await serving; } catch (AuthenticationException) { } catch (IOException) { }
        }
        Record("reject." + expectedErrors + "=1");
    }

    private static async Task CancelHandshake(CancellationToken token)
    {
        (PipeStream left, PipeStream right) = PipeStream.CreatePair();
        using (right)
        using (SslStream client = new(left))
        using (CancellationTokenSource cancel = CancellationTokenSource.CreateLinkedTokenSource(token))
        {
            Task pending = client.AuthenticateAsClientAsync(new SslClientAuthenticationOptions { TargetHost = "localhost" }, cancel.Token);
            cancel.Cancel();
            try { await pending; throw new InvalidOperationException("Handshake ignored cancellation"); }
            catch (OperationCanceledException) when (cancel.IsCancellationRequested) { }
        }
        Record("handshake.cancel=1");
    }

    private static async Task HttpsAndWebSocket(X509Certificate2 certificate, CancellationToken token)
    {
        using TcpListener listener = new(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        Task serving = ObserveServer(ServeTls(listener, certificate, token));
        using SocketsHttpHandler handler = new()
        {
            UseProxy = false,
            SslOptions = new SslClientAuthenticationOptions { CertificateChainPolicy = Trust(certificate) }
        };
        using HttpClient http = new(handler);
        string response = await http.GetStringAsync($"https://localhost:{port}/", token);
        Require(response == "standard SslStream", "HTTPS payload");
        using ClientWebSocket socket = new();
        // The supplied invoker is a standard SocketsHttpHandler. No custom
        // ConnectCallback, stream, scheme rewrite or platform TLS adapter.
        await socket.ConnectAsync(new Uri($"wss://localhost:{port}/echo"), http, token);
        byte[] expected = "binary-联机"u8.ToArray();
        await socket.SendAsync(expected, WebSocketMessageType.Binary, true, token);
        byte[] buffer = new byte[256];
        WebSocketReceiveResult received = await socket.ReceiveAsync(buffer, token);
        Require(received.MessageType == WebSocketMessageType.Binary && received.EndOfMessage &&
            expected.AsSpan().SequenceEqual(buffer.AsSpan(0, received.Count)), "WSS binary echo");
        await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "done", token);
        await serving;
        Record("https.local=1");
        Record("wss.binary_close=1");
    }

    private static async Task ServeTls(TcpListener listener, X509Certificate2 certificate, CancellationToken token)
    {
        for (int request = 0; request < 2; request++)
        {
            using TcpClient connection = await listener.AcceptTcpClientAsync(token);
            using SslStream tls = new(new HeaderTraceStream(connection.GetStream()));
            await tls.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate, ApplicationProtocols = [SslApplicationProtocol.Http11]
            }, token);
            StringBuilder headers = new();
            byte[] one = new byte[1];
            while (!headers.ToString().EndsWith("\r\n\r\n", StringComparison.Ordinal))
            {
                Require(headers.Length < 16384 && await tls.ReadAsync(one, token) == 1, "HTTP headers");
                headers.Append((char)one[0]);
            }
            if (request == 0)
            {
                await tls.WriteAsync("HTTP/1.1 200 OK\r\nContent-Length: 18\r\nConnection: close\r\n\r\nstandard SslStream"u8.ToArray(), token);
            }
            else
            {
                string key = headers.ToString().Split("\r\n").Single(line => line.StartsWith("Sec-WebSocket-Key:", StringComparison.OrdinalIgnoreCase)).Split(':', 2)[1].Trim();
                string accept = Convert.ToBase64String(SHA1.HashData(Encoding.ASCII.GetBytes(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
                await tls.WriteAsync(Encoding.ASCII.GetBytes("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n"), token);
                using WebSocket webSocket = WebSocket.CreateFromStream(tls, true, null, TimeSpan.FromSeconds(20));
                byte[] buffer = new byte[256];
                WebSocketReceiveResult message = await webSocket.ReceiveAsync(buffer, token);
                await webSocket.SendAsync(buffer.AsMemory(0, message.Count), message.MessageType, message.EndOfMessage, token);
                WebSocketReceiveResult close = await webSocket.ReceiveAsync(buffer, token);
                Require(close.MessageType == WebSocketMessageType.Close, "WSS close request");
                await webSocket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "done", token);
            }
        }
    }

#if LIBNX_PROBE
    private static async Task PublicHttps(CancellationToken token)
    {
        using HttpClient http = new(new SocketsHttpHandler { UseProxy = false });
        string body = await http.GetStringAsync("https://example.com/", token);
        Require(body.Contains("Example Domain", StringComparison.Ordinal), "Public HTTPS response");
        Record("https.public_default_trust=1");
        foreach (string host in new[] { "self-signed.badssl.com", "wrong.host.badssl.com", "expired.badssl.com" })
        {
            try
            {
                using HttpResponseMessage response = await http.GetAsync("https://" + host + "/", token);
                throw new InvalidOperationException("Invalid public certificate accepted: " + host);
            }
            catch (HttpRequestException exception) when (exception.InnerException is AuthenticationException)
            {
                Record("https.reject." + host + "=1");
            }
        }
    }
#endif

    private sealed class HeaderTraceStream(Stream inner) : Stream
    {
        private bool traced;
        public override bool CanRead => inner.CanRead;
        public override bool CanWrite => inner.CanWrite;
        public override bool CanSeek => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() => inner.Flush();
        public override Task FlushAsync(CancellationToken token) => inner.FlushAsync(token);
        public override int Read(byte[] buffer, int offset, int count) => ReadAsync(buffer.AsMemory(offset, count)).AsTask().GetAwaiter().GetResult();
        public override void Write(byte[] buffer, int offset, int count) => inner.Write(buffer, offset, count);
        public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken token)
            => ReadAsync(buffer.AsMemory(offset, count), token).AsTask();
        public override Task WriteAsync(byte[] buffer, int offset, int count, CancellationToken token)
            => inner.WriteAsync(buffer, offset, count, token);
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken token = default)
        {
            int count = await inner.ReadAsync(buffer, token);
            if (!traced && count > 0)
            {
                Record("tcp.client_hello_prefix=" + Convert.ToHexString(buffer.Span[..Math.Min(count, 16)]));
                traced = true;
            }
            return count;
        }
        public override ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken token = default)
            => inner.WriteAsync(buffer, token);
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        protected override void Dispose(bool disposing) { if (disposing) inner.Dispose(); base.Dispose(disposing); }
    }

    // No socket exists underneath these streams. Small reads force SslStream
    // to reassemble TLS records instead of relying on a descriptor shortcut.
    private sealed class PipeStream(Channel<byte[]> input, Channel<byte[]> output) : Stream
    {
        private byte[]? current;
        private int offset;
        internal bool IsDisposed { get; private set; }
        internal static (PipeStream, PipeStream) CreatePair()
        {
            Channel<byte[]> first = Channel.CreateUnbounded<byte[]>();
            Channel<byte[]> second = Channel.CreateUnbounded<byte[]>();
            return (new(first, second), new(second, first));
        }
        public override bool CanRead => !IsDisposed;
        public override bool CanWrite => !IsDisposed;
        public override bool CanSeek => false;
        public override long Length => throw new NotSupportedException();
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() { }
        public override Task FlushAsync(CancellationToken token) => Task.CompletedTask;
        public override int Read(byte[] buffer, int start, int count) => ReadAsync(buffer.AsMemory(start, count)).AsTask().GetAwaiter().GetResult();
        public override void Write(byte[] buffer, int start, int count) => WriteAsync(buffer.AsMemory(start, count)).AsTask().GetAwaiter().GetResult();
        public override Task<int> ReadAsync(byte[] buffer, int start, int count, CancellationToken token)
            => ReadAsync(buffer.AsMemory(start, count), token).AsTask();
        public override Task WriteAsync(byte[] buffer, int start, int count, CancellationToken token)
            => WriteAsync(buffer.AsMemory(start, count), token).AsTask();
        public override async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken token = default)
        {
            ObjectDisposedException.ThrowIf(IsDisposed, this);
            token.ThrowIfCancellationRequested();
            if (buffer.IsEmpty) return 0;
            while (current == null || offset == current.Length)
            {
                try { current = await input.Reader.ReadAsync(token); offset = 0; }
                catch (ChannelClosedException) { return 0; }
            }
            int count = Math.Min(Math.Min(buffer.Length, current.Length - offset), 307);
            current.AsMemory(offset, count).CopyTo(buffer);
            offset += count;
            return count;
        }
        public override ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken token = default)
        {
            ObjectDisposedException.ThrowIf(IsDisposed, this);
            return output.Writer.WriteAsync(buffer.ToArray(), token);
        }
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        protected override void Dispose(bool disposing)
        {
            if (disposing) { IsDisposed = true; output.Writer.TryComplete(); }
            base.Dispose(disposing);
        }
    }
}

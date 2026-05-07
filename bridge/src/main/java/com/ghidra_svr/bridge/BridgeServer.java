package com.ghidra_svr.bridge;

import java.net.ServerSocket;
import java.net.Socket;

/**
 * Binds a TCP server socket and spawns a {@link BridgeConnection} thread for
 * each incoming client.  In normal operation the Binary Ninja plugin is the only
 * client, so we accept multiple connections but keep a low-water design.
 *
 * Startup handshake:
 *   1. ServerSocket binds.
 *   2. Bridge prints "READY port=<N>" to stdout.
 *   3. C++ plugin reads that line, connects on the reported port, and begins
 *      sending newline-delimited JSON requests.
 */
public class BridgeServer {

    private final int requestedPort;

    public BridgeServer(int requestedPort) {
        this.requestedPort = requestedPort;
    }

    public void serve() throws Exception {
        // Bind on loopback only — the bridge should never be reachable from the network.
        try (ServerSocket server = new ServerSocket(requestedPort,
                /*backlog*/ 4,
                java.net.InetAddress.getLoopbackAddress())) {
            server.setReuseAddress(true);
            int boundPort = server.getLocalPort();
            System.err.println("[ghidra-bridge] listening on 127.0.0.1:" + boundPort);

            // Signal the C++ parent process that we are ready.
            System.out.println("READY port=" + boundPort);
            System.out.flush();

            while (!Thread.currentThread().isInterrupted()) {
                Socket client = server.accept();
                client.setTcpNoDelay(true);
                System.err.println("[ghidra-bridge] client connected from " + client.getRemoteSocketAddress());
                Thread t = new Thread(new BridgeConnection(client), "bridge-conn");
                t.setDaemon(true);
                t.start();
            }
        }
    }
}

package com.ghidra_svr.bridge;

import javax.net.ssl.*;
import java.security.cert.X509Certificate;

/**
 * Entry point.  Spawned by the Binary Ninja C++ plugin with:
 *
 *   java -cp "ghidra-bridge-0.1.0.jar:<ghidra-jars>/*"
 *        com.ghidra_svr.bridge.BridgeMain
 *        [--port <N>] [--trust-all]
 *
 * Writes "READY port=<N>" to stdout once the server socket is bound,
 * then all further I/O is JSON-over-TCP on the bound port.
 */
public class BridgeMain {

    public static void main(String[] args) throws Exception {
        int port = 13200;
        boolean trustAll = false;

        for (int i = 0; i < args.length; i++) {
            if ("--port".equals(args[i]) && i + 1 < args.length) {
                port = Integer.parseInt(args[++i]);
            } else if ("--trust-all".equals(args[i])) {
                trustAll = true;
            }
        }

        if (trustAll) {
            installTrustAllContext();
            System.err.println("[ghidra-bridge] WARNING: SSL certificate validation disabled");
        }

        System.err.println("[ghidra-bridge] build=addrmap-fix starting...");
        new BridgeServer(port).serve();
    }

    /** Installs a no-op TrustManager so the bridge accepts any server certificate.
     *  Use only on trusted internal networks or for development. */
    private static void installTrustAllContext() throws Exception {
        TrustManager[] trustAll = new TrustManager[]{
            new X509TrustManager() {
                public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
                public void checkClientTrusted(X509Certificate[] c, String a) {}
                public void checkServerTrusted(X509Certificate[] c, String a) {}
            }
        };
        SSLContext sc = SSLContext.getInstance("TLS");
        sc.init(null, trustAll, new java.security.SecureRandom());
        SSLContext.setDefault(sc);
        HttpsURLConnection.setDefaultSSLSocketFactory(sc.getSocketFactory());
    }
}

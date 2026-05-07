# binja-ghidra

A Binary Ninja plugin that connects to a Ghidra Server repository and imports its analysis — symbols, function names, and comments — directly into an open Binary Ninja binary view.

## What it does

Ghidra and Binary Ninja each have strengths. This plugin lets you use both on the same binary without manually copying names or comments between them. Connect to a running Ghidra Server, browse its repositories, and double-click any project file to pull its analysis into the currently open BN view.

**Imported data:**
- Function names and labels (user-defined, imported, and analysis-generated)
- EOL, pre, post, plate, and repeatable comments
- Function attributes: thunk, no-return, and inline flags (applied as BN tags)

## Architecture

```
Binary Ninja (C++ plugin)
    │  TCP / newline-delimited JSON
    ▼
ghidra-bridge-*.jar  (Java, runs as a subprocess)
    │  Java RMI / SSL
    ▼
Ghidra Server  (ghidraSvr, running on the network)
```

The plugin spawns a Java subprocess (the "bridge") on load. The bridge holds the RMI connection to the Ghidra Server and speaks a simple JSON protocol back to the plugin over a local TCP socket. This keeps all Java/RMI code out of the C++ process and lets the JVM start in the background while BN finishes loading.

### Components

| Path | Language | Role |
|------|----------|------|
| `plugin/` | C++ / Qt6 | Binary Ninja sidebar plugin |
| `bridge/` | Java 17 | Ghidra RMI client + JSON bridge server |

**Plugin (C++):**
- `plugin.cpp` — registers settings and the sidebar widget; eagerly starts the bridge JVM on load
- `GhidraConnection.cpp` — singleton; manages bridge lifecycle and all RMI-backed operations
- `BridgeProcess.cpp` — launches the bridge JAR as a subprocess with stdout/stderr pipes; reads the `READY port=N` handshake line
- `BridgeClient.cpp` — TCP client; sends JSON requests, receives responses, dispatches async events
- `SyncEngine.cpp` — applies a `GhidraDbExport` to a `BinaryView` (symbols, comments, flags)
- `ui/ProjectPanel.cpp` — sidebar widget: repo tree, connect dialog, activity log
- `ui/ConnectDialog.cpp` — host/port/user/password dialog

**Bridge (Java):**
- `BridgeMain.java` — argument parsing; starts the TCP server; prints `READY port=N` to stdout
- `BridgeServer.java` — accepts one TCP client connection and hands it a `BridgeConnection`
- `BridgeConnection.java` — JSON request dispatcher; serialises Ghidra API responses to JSON
- `GhidraSession.java` — authenticated RMI session; wraps `RemoteRepositoryServerHandle`
- `EventStreamer.java` — background thread per open repo; pushes `RepositoryChangeEvent`s to the plugin as async JSON events
- `DatabaseExporter.java` — reads a `ManagedBufferFileHandle` (Ghidra's remote DB buffer) directly via `db.jar`; extracts symbol, comment, and function-flag tables without requiring `SoftwareModeling.jar` or any processor JARs

## Prerequisites

| Dependency | Notes |
|------------|-------|
| Binary Ninja (commercial) | Tested against the version matching the auto-fetched API commit in `api_REVISION.txt` |
| Ghidra Server | Tested with Ghidra 12.0.4. The server must be running and reachable over RMI/SSL |
| Java 17+ JDK | Eclipse Adoptium JDK 21 recommended; path set in BN settings |
| Visual Studio 2022+ | MSVC C++ toolchain; LLVM/clang should also work with minor CMake edits |
| Qt 6.7+ | Required for the BN UI plugin; `qmake` must be on `PATH` at build time |
| Gradle (via wrapper) | The bridge uses the Gradle wrapper — no separate install needed |

## Building

```bat
rem Full clean build + install into BN plugins folder
build.bat clean install

rem Incremental build of both components
build.bat

rem Build only the C++ plugin
build.bat plugin

rem Build only the Java bridge
build.bat bridge

rem Build and install without rebuilding bridge
build.bat plugin install
```

Edit the paths at the top of `build.bat` to match your environment before first use:

```bat
set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
set "VSDEVCMD=..."   rem path to VsDevCmd.bat
set "Qt6_DIR=C:\qt\v6.7.2\lib\cmake\Qt6"
set "BN_INSTALL=C:\Program Files\Vector35\BinaryNinja"
```

The C++ build uses CMake FetchContent to clone `binaryninja-api` at the exact commit recorded in `%BN_INSTALL%\api_REVISION.txt`, so the plugin ABI always matches the installed BN version.

## Configuration

After installing, set these in Binary Ninja's settings (`Edit → Preferences → Settings`, search "Ghidra"):

| Setting | Description |
|---------|-------------|
| `ghidra.javaExe` | Full path to `java.exe` |
| `ghidra.ghidraHome` | Root of your Ghidra installation (contains `Ghidra/Framework/…`) |
| `ghidra.trustAllCerts` | Set `true` if your Ghidra Server uses a self-signed certificate |
| `ghidra.defaultHost` | Pre-fills the Connect dialog |
| `ghidra.defaultPort` | Default: `13100` |
| `ghidra.defaultUser` | Pre-fills the Connect dialog |

## Usage

1. Open a binary in Binary Ninja.
2. Open the **Ghidra** sidebar (the red "G" icon).
3. Click **Connect…** and enter your server credentials.
4. The repository tree populates. Click the expand arrow (▶) on a repository to reveal its folders and project files.
5. Project files appear **bold and blue**. Double-click one to import its analysis into the currently open binary view.
6. The activity log shows import progress and the address range of applied symbols.

**Prerequisite for step 5:** the program file must be committed to the Ghidra Server repository (not just open locally in Ghidra). In Ghidra: right-click the file in the Project window → *Version Control → Add to Version Control…*.

## Bridge protocol

The plugin and bridge communicate over a local TCP socket using newline-delimited JSON. Every request carries an integer `id` and a string `op`; every response echoes the `id`. Async events (server-side repository changes) carry an `"event"` key instead.

Supported ops: `ping`, `connect`, `disconnect`, `status`, `list_repos`, `open_repo`, `close_repo`, `list_items`, `get_subfolders`, `get_versions`, `get_checkouts`, `checkout`, `terminate_checkout`, `open_db`.

`open_db` is the heavy operation: it fetches the full Ghidra database buffer over RMI, then reads the Symbols, Comments, and Function Data tables directly from the raw DB layer (no Ghidra headless analysis required). The result is streamed back as a single JSON response with `symbols`, `comments`, and `func_flags` arrays.

## Known limitations / pending work

- **Address rebase**: Ghidra stores addresses as raw virtual addresses. If Ghidra and BN loaded the binary at different image bases, imported symbols will land at the wrong addresses. A rebase step is in progress.
- **Write-back**: pushing BN analysis (renames, comments) back to Ghidra is not yet implemented.
- **Authentication**: only username + password is supported. PKI and SSH-key callbacks are not yet handled.
- **Single address space**: the `DatabaseExporter` assumes a single RAM address space. Overlay spaces or Harvard architectures may produce incorrect addresses.

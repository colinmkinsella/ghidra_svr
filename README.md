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
| Binary Ninja (commercial) | Tested against the version matching `api_REVISION.txt` in the BN install |
| Ghidra Server | Tested with Ghidra 12.0.4. Must be running and reachable over RMI/SSL |
| Java 17+ JDK | Eclipse Adoptium JDK 21 recommended |
| CMake 3.24+ | |
| Ninja | |
| C++ compiler | MSVC 2022+ on Windows; clang on macOS; gcc/clang on Linux |
| Qt 6.7+ | See [Qt setup](#qt-setup) below; `qmake` must be on `PATH` at build time |
| Gradle (via wrapper) | The bridge uses the Gradle wrapper — no separate install needed |
| **Poetry** *(Qt build only)* | Required only when building Qt from the `qt-build` submodule. Install with `pip install poetry` or `pipx install poetry`. |
| **libclang 19** *(Qt build only)* | Required by Qt's build system. See `qt-build/README.md` for download instructions. |

## Qt setup

The plugin links against the same Qt 6 build that Binary Ninja uses. You have two options:

**Option A — Use an existing Qt install** (fastest if you already have Qt)

Pass `Qt6_DIR` pointing at your Qt CMake directory:
```sh
Qt6_DIR=/path/to/Qt/6.x.y/clang_64/lib/cmake/Qt6 ./build.sh
```
On macOS the build script auto-detects Qt if it was installed by the Qt online installer under `/usr/local/Qt*`.

**Option B — Build Qt from the `qt-build` submodule** (~1-2 hours, once per machine)

The `qt-build` submodule (Vector35's Qt build scripts) compiles Qt 6 with Binary Ninja's patches. It requires Poetry and libclang 19 (see Prerequisites above and `qt-build/README.md`).

Qt is installed to `qt/<version>/<compiler>/` inside the repo:

| Platform | Install path |
|----------|-------------|
| macOS | `qt/6.10.1/clang_64/` |
| Linux x86-64 | `qt/6.10.1/gcc_64/` |
| Windows | `qt/6.10.1/msvc2022_64/` |

```sh
# First time on a new machine:
./build.sh qt          # compiles Qt — takes 1-2 hours

# All subsequent builds (Qt cached in qt/, reused automatically):
./build.sh
```

The `qt` step is only needed once. CMake and the build scripts detect the built Qt in `qt/` on every subsequent run and skip the submodule entirely. The `qt/` directory is gitignored.

## Building

### Fresh checkout

```sh
git clone https://github.com/your-org/ghidra_svr
cd ghidra_svr
git submodule update --init   # populates binaryninja-api and qt-build (~seconds)
```

Then follow the Qt setup above (Option A or B), and run:

```sh
./build.sh install
```

### macOS / Linux

```sh
# Incremental build of both components
./build.sh

# Full clean rebuild + install into BN plugins folder
./build.sh clean install

# Build only the C++ plugin
./build.sh plugin

# Build only the Java bridge
./build.sh bridge

# Build Qt once on a machine without Qt installed
./build.sh qt
```

Environment variables (all optional — the script sets sensible defaults):

```sh
BN_INSTALL=/Applications/Binary\ Ninja.app/Contents/MacOS
Qt6_DIR=/usr/local/Qt-6.7.2/lib/cmake/Qt6
```

### Windows

Edit the paths at the top of `build.bat` to match your environment before first use:

```bat
set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
set "Qt6_DIR=C:\qt\v6.7.2\lib\cmake\Qt6"
set "BN_INSTALL=C:\Program Files\Vector35\BinaryNinja"
```

```bat
rem Incremental build of both components
build.bat

rem Full clean rebuild + install into BN plugins folder
build.bat clean install

rem Build only the C++ plugin
build.bat plugin

rem Build only the Java bridge
build.bat bridge

rem Build Qt once on a machine without Qt installed
build.bat qt
```

The C++ build uses CMake FetchContent to clone `binaryninja-api` at the exact commit recorded in `api_REVISION.txt`, so the plugin ABI always matches the installed BN version. Ghidra is downloaded automatically by CMake on first configure if `GHIDRA_HOME` is not set.

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

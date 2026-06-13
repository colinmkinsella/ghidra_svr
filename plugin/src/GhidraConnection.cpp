#include "GhidraConnection.h"
#include <binaryninjaapi.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <dirent.h>
#endif

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

GhidraConnection& GhidraConnection::instance() {
    static GhidraConnection inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Settings helpers
// ---------------------------------------------------------------------------

std::string GhidraConnection::bridgeMode() const {
    return BinaryNinja::Settings::Instance()->Get<std::string>("ghidra.bridgeMode");
}
int GhidraConnection::bridgePort() const {
    return static_cast<int>(
        BinaryNinja::Settings::Instance()->Get<int64_t>("ghidra.bridgePort"));
}
std::string GhidraConnection::javaExe() const {
    return BinaryNinja::Settings::Instance()->Get<std::string>("ghidra.javaExe");
}
std::string GhidraConnection::bridgeJar() const {
    return BinaryNinja::Settings::Instance()->Get<std::string>("ghidra.bridgeJar");
}
std::string GhidraConnection::ghidraHome() const {
    return BinaryNinja::Settings::Instance()->Get<std::string>("ghidra.ghidraHome");
}
bool GhidraConnection::trustAll() const {
    return BinaryNinja::Settings::Instance()->Get<bool>("ghidra.trustAllCerts");
}

// ---------------------------------------------------------------------------
// Auto-detection helpers (local mode only)
// ---------------------------------------------------------------------------

static std::string pluginDir() {
#ifdef _WIN32
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GhidraConnection::instance), &hMod);
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(hMod, buf, MAX_PATH);
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string path(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, path.data(), n, nullptr, nullptr);
    size_t slash = path.find_last_of("/\\");
    return (slash != std::string::npos) ? path.substr(0, slash) : std::string{};
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&GhidraConnection::instance), &info) && info.dli_fname) {
        std::string path = info.dli_fname;
        size_t slash = path.find_last_of('/');
        return (slash != std::string::npos) ? path.substr(0, slash) : std::string{};
    }
    return {};
#endif
}

/*static*/ std::string GhidraConnection::autoDetectGhidraHome() {
    std::string dir = pluginDir();
    for (int up = 0; up < 3 && !dir.empty(); ++up) {
#ifdef _WIN32
        WIN32_FIND_DATAW fd;
        std::wstring pattern = std::wstring(dir.begin(), dir.end()) + L"\\ghidra_*_PUBLIC";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            std::wstring found = std::wstring(dir.begin(), dir.end()) + L"\\" + fd.cFileName;
            FindClose(h);
            int n = WideCharToMultiByte(CP_UTF8, 0, found.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string s(n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, found.c_str(), -1, s.data(), n, nullptr, nullptr);
            return s;
        }
#else
        DIR* d = opendir(dir.c_str());
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                std::string name = ent->d_name;
                if (name.size() > 14 &&
                    name.substr(0, 7)             == "ghidra_" &&
                    name.substr(name.size() - 7)  == "_PUBLIC") {
                    closedir(d);
                    return dir + "/" + name;
                }
            }
            closedir(d);
        }
#endif
        size_t sep = dir.find_last_of("/\\");
        if (sep == std::string::npos) break;
        dir = dir.substr(0, sep);
    }
#ifdef GHIDRA_HOME_DEFAULT
    if (sizeof(GHIDRA_HOME_DEFAULT) > 1)
        return GHIDRA_HOME_DEFAULT;
#endif
    return {};
}

/*static*/ std::string GhidraConnection::autoDetectBridgeJar() {
    std::string dir = pluginDir();
    if (!dir.empty()) {
#ifdef _WIN32
        return dir + "\\ghidra-bridge-0.1.0.jar";
#else
        return dir + "/ghidra-bridge-0.1.0.jar";
#endif
    }
    return {};
}

// ---------------------------------------------------------------------------
// Bridge lifecycle
// ---------------------------------------------------------------------------

bool GhidraConnection::startLocalBridge(std::string& errorOut) {
    // No-op if already running.
    if (m_process && m_process->isRunning() && m_client && m_client->isConnected())
        return true;

    // Clean up any stale state.
    if (m_client)  { m_client->disconnect();  m_client.reset();  }
    if (m_process) { m_process->stop();        m_process.reset(); }

    std::string jar = bridgeJar();
    if (jar.empty()) jar = autoDetectBridgeJar();
    if (jar.empty()) {
        errorOut = "Bridge JAR not found. Set ghidra.bridgeJar in Settings, or build with "
                   "'./build.sh bridge' and install.";
        return false;
    }

    std::string home = ghidraHome();
    if (home.empty()) home = autoDetectGhidraHome();
    if (home.empty()) {
        errorOut = "Ghidra installation not found. Set ghidra.ghidraHome in Settings.";
        return false;
    }
    BinaryNinja::LogInfo("Ghidra: local bridge — Ghidra at %s", home.c_str());

    m_process = std::make_unique<BridgeProcess>();
    if (!m_process->start(javaExe(), jar, home, trustAll(), errorOut)) {
        m_process.reset();
        return false;
    }

    m_client = std::make_unique<BridgeClient>();
    if (!m_client->connect("127.0.0.1", m_process->port(), errorOut)) {
        m_process->stop();
        m_process.reset();
        m_client.reset();
        return false;
    }

    m_client->setEventCallback([this](nlohmann::json evt) {
        onBridgeEvent(std::move(evt));
    });

    BinaryNinja::LogInfo("Ghidra: local bridge ready on port %d", m_process->port());
    return true;
}

bool GhidraConnection::connectBridge(const std::string& remoteHost, std::string& errorOut) {
    if (isBridgeConnected()) return true;

    if (bridgeMode() == "local") {
        return startLocalBridge(errorOut);
    }

    // Remote mode: connect to the bridge service on the Ghidra server machine.
    int port = bridgePort();
    BinaryNinja::LogInfo("Ghidra: remote bridge at %s:%d", remoteHost.c_str(), port);

    m_client = std::make_unique<BridgeClient>();
    if (!m_client->connect(remoteHost, port, errorOut)) {
        m_client.reset();
        errorOut = "Cannot reach Ghidra bridge at " + remoteHost + ":" +
                   std::to_string(port) +
                   " — is start-bridge.sh running on the server? (" + errorOut + ")";
        return false;
    }

    m_client->setEventCallback([this](nlohmann::json evt) {
        onBridgeEvent(std::move(evt));
    });

    BinaryNinja::LogInfo("Ghidra: remote bridge connected");
    return true;
}

void GhidraConnection::disconnectBridge() {
    disconnectFromServer();
    if (m_client)  { m_client->disconnect();  m_client.reset();  }
    if (m_process) { m_process->stop();        m_process.reset(); }
}

bool GhidraConnection::isBridgeConnected() const {
    if (!m_client || !m_client->isConnected()) return false;
    // In local mode also verify the child process is still alive.
    if (bridgeMode() == "local" && m_process && !m_process->isRunning()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Server connection
// ---------------------------------------------------------------------------

bool GhidraConnection::connectToServer(const std::string& host, int port,
                                        const std::string& user,
                                        const std::string& password,
                                        std::string& errorOut) {
    // Bridge host matches the Ghidra server host — both run on the same machine.
    if (!isBridgeConnected()) {
        if (!connectBridge(host, errorOut)) return false;
    }

    try {
        auto resp = m_client->sendSync({
            {"op",       "connect"},
            {"host",     host},
            {"port",     port},
            {"user",     user},
            {"password", password}
        });
        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "unknown error");
            return false;
        }
        m_serverConnected = true;
        m_user     = user;
        m_host     = host;
        m_port     = port;
        m_password = password; // held in memory for upload_binary, cleared on disconnect
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

void GhidraConnection::disconnectFromServer() {
    if (m_client && m_serverConnected) {
        try {
            m_client->sendSync({{"op", "disconnect"}}, 3000);
        } catch (...) {}
    }
    m_serverConnected = false;
    m_user.clear();
    m_host.clear();
    m_port = 0;
    m_password.clear();
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

static std::string toHex(uint64_t v) {
    char buf[20];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)v);
    return buf;
}

static uint64_t parseHexAddr(const std::string& s) {
    if (s.size() < 2) return 0;
    const char* p = s.c_str();
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    return std::strtoull(p, nullptr, 16);
}

// ---------------------------------------------------------------------------
// Repository operations
// ---------------------------------------------------------------------------

std::vector<std::string> GhidraConnection::listRepos(std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({{"op", "list_repos"}});
        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "list_repos failed");
            return {};
        }
        std::vector<std::string> result;
        for (auto& r : resp["repos"]) result.push_back(r.get<std::string>());
        return result;
    } catch (const std::exception& e) {
        errorOut = e.what(); return {};
    }
}

bool GhidraConnection::openRepo(const std::string& repo, std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({{"op", "open_repo"}, {"repo", repo}});
        if (!resp.value("ok", false)) { errorOut = resp.value("error", "open_repo failed"); return false; }
        return true;
    } catch (const std::exception& e) { errorOut = e.what(); return false; }
}

void GhidraConnection::closeRepo(const std::string& repo) {
    try { m_client->sendSync({{"op", "close_repo"}, {"repo", repo}}, 3000); } catch (...) {}
}

std::vector<RepoItem> GhidraConnection::listItems(const std::string& repo,
                                                    const std::string& folder,
                                                    std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "list_items"}, {"repo", repo}, {"folder", folder}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return {}; }
        std::vector<RepoItem> out;
        for (auto& j : resp["items"]) out.push_back(itemFromJson(j));
        return out;
    } catch (const std::exception& e) { errorOut = e.what(); return {}; }
}

std::vector<std::string> GhidraConnection::getSubfolders(const std::string& repo,
                                                           const std::string& folder,
                                                           std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "get_subfolders"}, {"repo", repo}, {"folder", folder}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return {}; }
        std::vector<std::string> out;
        for (auto& j : resp["subfolders"]) out.push_back(j.get<std::string>());
        return out;
    } catch (const std::exception& e) { errorOut = e.what(); return {}; }
}

std::vector<VersionInfo> GhidraConnection::getVersions(const std::string& repo,
                                                         const std::string& folder,
                                                         const std::string& item,
                                                         std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "get_versions"}, {"repo", repo}, {"folder", folder}, {"item", item}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return {}; }
        std::vector<VersionInfo> out;
        for (auto& j : resp["versions"]) out.push_back(versionFromJson(j));
        return out;
    } catch (const std::exception& e) { errorOut = e.what(); return {}; }
}

std::vector<CheckoutInfo> GhidraConnection::getCheckouts(const std::string& repo,
                                                           const std::string& folder,
                                                           const std::string& item,
                                                           std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "get_checkouts"}, {"repo", repo}, {"folder", folder}, {"item", item}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return {}; }
        std::vector<CheckoutInfo> out;
        for (auto& j : resp["checkouts"]) out.push_back(checkoutFromJson(j));
        return out;
    } catch (const std::exception& e) { errorOut = e.what(); return {}; }
}

CheckoutInfo GhidraConnection::checkout(const std::string& repo,
                                         const std::string& folder,
                                         const std::string& item,
                                         const std::string& checkoutType,
                                         std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "checkout"}, {"repo", repo}, {"folder", folder},
            {"item", item},     {"type", checkoutType}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return {}; }
        return checkoutFromJson(resp["checkout"]);
    } catch (const std::exception& e) { errorOut = e.what(); return {}; }
}

GhidraDbExport GhidraConnection::openDatabase(const std::string& repo,
                                               const std::string& folder,
                                               const std::string& item,
                                               int version,
                                               std::string& errorOut) {
    try {
        // Generous timeout: reading a large DB over RMI can take tens of seconds.
        auto resp = m_client->sendSync({
            {"op",      "open_db"},
            {"repo",    repo},
            {"folder",  folder},
            {"item",    item},
            {"version", version}
        }, /*timeoutMs=*/120'000);

        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "open_db failed");
            return {};
        }

        GhidraDbExport out;
        out.imageBase = parseHexAddr(resp.value("image_base", std::string{"0x0"}));

        if (resp.contains("diag")) {
            for (auto& d : resp["diag"])
                out.diag.push_back(d.get<std::string>());
        }

        for (auto& j : resp["symbols"]) {
            GhidraSymbol s;
            s.key         = j.value("key",  int64_t{0});
            s.name        = j.value("name", std::string{});
            s.addr        = parseHexAddr(j.value("addr", std::string{"0x0"}));
            s.type        = static_cast<GhidraSymbolType>(j.value("type",   0));
            s.source      = static_cast<GhidraSymbolSource>(j.value("source", 0));
            s.namespaceId = j.value("ns",   int64_t{0});
            out.symbols.push_back(std::move(s));
        }

        for (auto& j : resp["comments"]) {
            GhidraComment c;
            c.addr       = parseHexAddr(j.value("addr", std::string{"0x0"}));
            c.encodedKey = j.value("key",  std::string{});
            c.eol        = j.value("eol",   std::string{});
            c.pre   = j.value("pre",   std::string{});
            c.post  = j.value("post",  std::string{});
            c.plate = j.value("plate", std::string{});
            c.rep   = j.value("rep",   std::string{});
            out.comments.push_back(std::move(c));
        }

        for (auto& j : resp["func_flags"]) {
            GhidraFuncFlags f;
            f.key      = j.value("key",    int64_t{0});
            f.thunk    = j.value("thunk",  false);
            f.noReturn = j.value("no_ret", false);
            f.isInline = j.value("inline", false);
            out.funcFlags.push_back(f);

            // NEW: signature fields
            std::string cc      = j.value("cc",          std::string{});
            std::string retType = j.value("ret_type",    std::string{});
            int64_t retTypeId   = j.value("ret_type_id", int64_t{-1});
            if (!cc.empty() || !retType.empty()) {
                GhidraFuncSig sig;
                sig.key               = f.key;
                sig.callingConvention = cc;
                sig.returnTypeName    = retType;
                sig.returnTypeId      = retTypeId;
                out.funcSigs.push_back(std::move(sig));
            }
        }

        if (resp.contains("equates")) {
            for (auto& j : resp["equates"]) {
                GhidraEquate eq;
                eq.id    = j.value("id",    int64_t{0});
                eq.name  = j.value("name",  std::string{});
                eq.value = j.value("value", int64_t{0});
                if (j.contains("refs")) {
                    for (auto& r : j["refs"]) {
                        GhidraEquateRef ref;
                        ref.addr    = parseHexAddr(r.value("addr",     std::string{"0x0"}));
                        ref.opIndex = r.value("op_index", 0);
                        eq.refs.push_back(std::move(ref));
                    }
                }
                out.equates.push_back(std::move(eq));
            }
        }

        if (resp.contains("bookmarks")) {
            for (auto& j : resp["bookmarks"]) {
                GhidraBookmark bm;
                bm.type     = j.value("type",     std::string{});
                bm.addr     = parseHexAddr(j.value("addr",     std::string{"0x0"}));
                bm.category = j.value("category", std::string{});
                bm.comment  = j.value("comment",  std::string{});
                out.bookmarks.push_back(std::move(bm));
            }
        }

        if (resp.contains("parameters")) {
            for (auto& j : resp["parameters"]) {
                GhidraParameter p;
                p.key      = j.value("key",       int64_t{0});
                p.funcAddr = parseHexAddr(j.value("func_addr", std::string{"0x0"}));
                p.name     = j.value("name",      std::string{});
                p.isParam  = j.value("is_param",  true);
                p.ordinal  = j.value("ordinal",   0);
                p.typeName = j.value("type_name", std::string{});
                p.typeId   = j.value("type_id",   int64_t{-1});
                out.parameters.push_back(std::move(p));
            }
        }

        if (resp.contains("data_types")) {
            for (auto& j : resp["data_types"]) {
                GhidraDataType dt;
                dt.id      = j.value("id",      int64_t{0});
                dt.kind    = j.value("kind",    std::string{});
                dt.name    = j.value("name",    std::string{});
                dt.comment = j.value("comment", std::string{});
                dt.size    = j.value("size",    0);
                dt.underlyingTypeId = j.value("underlying_type_id", int64_t{0});
                if (dt.kind == "typedef")
                    dt.underlyingTypeName = j.value("underlying_name", std::string{});
                if (j.contains("members")) {
                    for (auto& m : j["members"]) {
                        GhidraDataTypeMember mem;
                        mem.offset   = m.value("offset",    0);
                        mem.typeId   = m.value("type_id",  int64_t{0});
                        mem.name     = m.value("name",     std::string{});
                        mem.comment  = m.value("comment",  std::string{});
                        mem.size     = m.value("size",     0);
                        mem.ordinal  = m.value("ordinal",  0);
                        mem.typeName = m.value("type_name",std::string{});
                        dt.members.push_back(std::move(mem));
                    }
                }
                if (j.contains("values")) {
                    for (auto& v : j["values"]) {
                        GhidraEnumValue ev;
                        ev.name    = v.value("name",    std::string{});
                        ev.value   = v.value("value",   int64_t{0});
                        ev.comment = v.value("comment", std::string{});
                        dt.values.push_back(std::move(ev));
                    }
                }
                out.dataTypes.push_back(std::move(dt));
            }
        }

        if (resp.contains("data_items")) {
            for (auto& j : resp["data_items"]) {
                GhidraDataItem item;
                item.addr   = parseHexAddr(j.value("addr",    std::string{"0x0"}));
                item.typeId = j.value("type_id", int64_t{0});
                out.dataItems.push_back(std::move(item));
            }
        }

        if (resp.contains("xref_stats")) {
            auto& xs = resp["xref_stats"];
            out.xrefStats.fromCount = xs.value("from_count", 0);
            out.xrefStats.toCount   = xs.value("to_count",   0);
        }

        return out;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return {};
    }
}

std::vector<GhidraConnection::BinaryFile>
GhidraConnection::downloadBinary(const std::string& repo,
                                  const std::string& folder,
                                  const std::string& item,
                                  int version,
                                  std::string& errorOut)
{
    try {
        auto resp = m_client->sendSync({
            {"op",      "download_binary"},
            {"repo",    repo},
            {"folder",  folder},
            {"item",    item},
            {"version", version}
        }, /*timeoutMs=*/300'000);

        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "download_binary failed");
            return {};
        }

        std::vector<BinaryFile> out;
        for (auto& j : resp["files"]) {
            BinaryFile bf;
            bf.filename = j.value("filename", std::string{});
            std::string b64 = j.value("data", std::string{});
            auto buf = BinaryNinja::DataBuffer::FromBase64(b64);
            bf.bytes.assign(
                static_cast<const uint8_t*>(buf.GetData()),
                static_cast<const uint8_t*>(buf.GetData()) + buf.GetLength());
            out.push_back(std::move(bf));
        }
        return out;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return {};
    }
}

bool GhidraConnection::terminateCheckout(const std::string& repo,
                                          const std::string& folder,
                                          const std::string& item,
                                          int64_t checkoutId,
                                          std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "terminate_checkout"}, {"repo", repo},
            {"folder", folder}, {"item", item}, {"checkout_id", checkoutId}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return false; }
        return true;
    } catch (const std::exception& e) { errorOut = e.what(); return false; }
}

bool GhidraConnection::deleteItem(const std::string& repo,
                                   const std::string& folder,
                                   const std::string& item,
                                   std::string& errorOut) {
    try {
        auto resp = m_client->sendSync({
            {"op", "delete_item"}, {"repo", repo}, {"folder", folder}, {"item", item}
        });
        if (!resp.value("ok", false)) { errorOut = resp.value("error", ""); return false; }
        return true;
    } catch (const std::exception& e) { errorOut = e.what(); return false; }
}

bool GhidraConnection::uploadBinary(const std::string& repo,
                                     const std::string& folder,
                                     const std::string& item,
                                     const std::vector<uint8_t>& bytes,
                                     const std::string& comment,
                                     bool keepCheckout,
                                     const std::string& analysisJson,
                                     std::string& errorOut)
{
    if (!isBridgeConnected())  { errorOut = "Bridge not connected";    return false; }
    if (!m_serverConnected)    { errorOut = "Not connected to server"; return false; }
    if (bytes.empty())         { errorOut = "Binary data is empty";    return false; }

    // Base64-encode the bytes for JSON transport over the loopback bridge connection.
    auto buf = BinaryNinja::DataBuffer(bytes.data(), bytes.size());
    std::string b64 = buf.ToBase64();

    try {
        // analyzeHeadless runs Ghidra auto-analysis before committing — allow 11 minutes
        // (one more than the bridge's own 10-minute subprocess timeout).
        auto resp = m_client->sendSync({
            {"op",            "upload_binary"},
            {"repo",          repo},
            {"folder",        folder},
            {"item",          item},
            {"data",          b64},
            {"comment",       comment},
            {"keep_checkout", keepCheckout},
            {"host",          m_host},
            {"port",          m_port},
            {"user",          m_user},
            {"password",      m_password},
            {"analysis_json", analysisJson}
        }, /*timeoutMs=*/660'000);

        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "upload_binary failed");
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void GhidraConnection::setEventHandler(EventHandler h) {
    m_eventHandler = std::move(h);
}

void GhidraConnection::onBridgeEvent(nlohmann::json evt) {
    if (!m_eventHandler) return;
    GhidraEvent e;
    e.repo         = evt.value("repo",           std::string{});
    e.type         = evt.value("type",           std::string{});
    e.parentPath   = evt.value("parent_path",    std::string{});
    e.name         = evt.value("name",           std::string{});
    e.newParentPath= evt.value("new_parent_path",std::string{});
    e.newName      = evt.value("new_name",       std::string{});
    m_eventHandler(std::move(e));
}

// ---------------------------------------------------------------------------
// JSON deserialisers
// ---------------------------------------------------------------------------

RepoItem GhidraConnection::itemFromJson(const nlohmann::json& j) {
    RepoItem r;
    r.name        = j.value("name",         std::string{});
    r.parentPath  = j.value("parent_path",  std::string{});
    r.path        = j.value("path",         std::string{});
    r.fileId      = j.value("file_id",      std::string{});
    r.itemType    = j.value("item_type",    0);
    r.contentType = j.value("content_type", std::string{});
    r.version     = j.value("version",      -1);
    r.versionTime = j.value("version_time", int64_t{0});
    return r;
}

CheckoutInfo GhidraConnection::checkoutFromJson(const nlohmann::json& j) {
    CheckoutInfo c;
    c.id          = j.value("id",           int64_t{0});
    c.type        = j.value("type",         std::string{});
    c.user        = j.value("user",         std::string{});
    c.version     = j.value("version",      -1);
    c.time        = j.value("time",         int64_t{0});
    c.projectPath = j.value("project_path", std::string{});
    return c;
}

VersionInfo GhidraConnection::versionFromJson(const nlohmann::json& j) {
    VersionInfo v;
    v.version = j.value("version", -1);
    v.time    = j.value("time",    int64_t{0});
    v.user    = j.value("user",    std::string{});
    v.comment = j.value("comment", std::string{});
    return v;
}

// ---------------------------------------------------------------------------
// Write-back
// ---------------------------------------------------------------------------

void GhidraConnection::storeCheckinState(
    std::unordered_map<uint64_t, int64_t>     addrToKey,
    std::unordered_map<uint64_t, std::string> addrToOriginalName,
    std::unordered_map<uint64_t, std::string> addrToCommentKey,
    std::unordered_map<uint64_t, std::string> addrToOriginalComment,
    std::unordered_map<uint64_t, std::string> addrToOriginalFuncComment,
    std::unordered_map<uint64_t, std::string> addrToCommentField,
    std::vector<GhidraDataType>               ghidraDataTypes,
    std::vector<GhidraParameter>              parameters,
    std::vector<GhidraBookmark>               bookmarks,
    std::vector<GhidraDataItem>               dataItems,
    std::vector<GhidraEquate>                 equates,
    std::vector<GhidraFuncSig>                funcSigs,
    uint64_t imageBase,
    const std::string& repo,
    const std::string& folder,
    const std::string& item)
{
    m_addrToKey                 = std::move(addrToKey);
    m_addrToOriginalName        = std::move(addrToOriginalName);
    m_addrToCommentKey          = std::move(addrToCommentKey);
    m_addrToOriginalComment     = std::move(addrToOriginalComment);
    m_addrToOriginalFuncComment = std::move(addrToOriginalFuncComment);
    m_addrToCommentField        = std::move(addrToCommentField);
    m_ghidraTypesByName.clear();
    for (auto& dt : ghidraDataTypes)
        m_ghidraTypesByName[dt.name] = dt;

    // Parameter baseline — store Ghidra VAs; rebase happens in collectCheckinChanges
    m_paramOriginalNameByKey.clear();
    m_paramOriginalTypeByKey.clear();
    m_paramKeyByAddrOrdinal.clear();
    m_newParamBaseline.clear();    // reset new-param tracking on fresh checkout
    m_newRegLocalBaseline.clear(); // reset new register-local tracking on fresh checkout
    for (const auto& p : parameters) {
        m_paramOriginalNameByKey[p.key] = p.name;
        m_paramKeyByAddrOrdinal[p.funcAddr][(int)p.ordinal] = p.key;
        if (!p.typeName.empty())
            m_paramOriginalTypeByKey[p.key] = p.typeName;
    }

    // Bookmark baseline — mirror SyncEngine's BN tag encoding, store Ghidra VAs
    m_ghidraBookmarkKeys.clear();
    m_ghidraTagTypeNames.clear();
    for (const auto& bm : bookmarks) {
        std::string tagTypeName = "Ghidra: " + bm.type;
        std::string tagData;
        if (!bm.category.empty()) tagData += "[" + bm.category + "] ";
        tagData += bm.comment;
        m_ghidraBookmarkKeys.insert({bm.addr, tagTypeName, tagData});
        m_ghidraTagTypeNames.insert(tagTypeName);
    }

    // Data item baseline — store Ghidra VAs; rebase in collectCheckinChanges
    m_ghidraDataItemAddrs.clear();
    for (const auto& item2 : dataItems)
        m_ghidraDataItemAddrs.insert(item2.addr);

    // Equate baseline — id → name, id → value, id → ref set
    m_equateOriginalNameById.clear();
    m_equateValueById.clear();
    m_equateRefsByEquateId.clear();
    for (const auto& eq : equates) {
        m_equateOriginalNameById[eq.id] = eq.name;
        m_equateValueById[eq.id]        = eq.value;
        // Store refs as Ghidra VAs (rebased to BN addr in collectCheckinChanges)
        for (const auto& ref : eq.refs)
            m_equateRefsByEquateId[eq.id].insert({ref.addr, ref.opIndex});
    }

    // Function signature baseline
    m_funcOriginalCC.clear();
    m_funcOriginalRetType.clear();
    m_funcReturnTypeId.clear();
    for (const auto& sig : funcSigs) {
        if (!sig.callingConvention.empty())
            m_funcOriginalCC[sig.key]      = sig.callingConvention;
        if (!sig.returnTypeName.empty())
            m_funcOriginalRetType[sig.key] = sig.returnTypeName;
        m_funcReturnTypeId[sig.key]        = sig.returnTypeId;
    }

    m_imageBase                 = imageBase;
    m_checkinRepo               = repo;
    m_checkinFolder             = folder;
    m_checkinItem               = item;
}

void GhidraConnection::clearCheckinState()
{
    m_addrToKey.clear();
    m_addrToOriginalName.clear();
    m_addrToCommentKey.clear();
    m_addrToOriginalComment.clear();
    m_addrToOriginalFuncComment.clear();
    m_addrToCommentField.clear();
    m_ghidraTypesByName.clear();
    m_paramOriginalNameByKey.clear();
    m_paramOriginalTypeByKey.clear();
    m_paramKeyByAddrOrdinal.clear();
    m_newParamBaseline.clear();
    m_newRegLocalBaseline.clear();
    m_ghidraBookmarkKeys.clear();
    m_ghidraTagTypeNames.clear();
    m_ghidraDataItemAddrs.clear();
    m_equateOriginalNameById.clear();
    m_equateValueById.clear();
    m_equateRefsByEquateId.clear();
    m_funcOriginalCC.clear();
    m_funcOriginalRetType.clear();
    m_funcReturnTypeId.clear();
    m_imageBase    = 0;
    m_checkinRepo.clear();
    m_checkinFolder.clear();
    m_checkinItem.clear();
}

/** Returns true if @p n is an auto-generated symbol name that should not be
 *  treated as a user-defined rename (e.g. sub_1234, FUN_00401234, etc.). */
static bool isAutoGeneratedSymbolName(const std::string& n)
{
    // Returns true iff every character from `offset` onward is a hex digit
    // and there are at least `minLen` such characters.
    auto hexSuffix = [&](size_t offset, size_t minLen = 4) -> bool {
        if (n.size() < offset + minLen) return false;
        for (size_t k = offset; k < n.size(); ++k) {
            char c = n[k];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    };
    if (n.rfind("sub_", 0) == 0 && hexSuffix(4)) return true;
    if (n.rfind("FUN_", 0) == 0 && hexSuffix(4)) return true;
    if (n.rfind("off_", 0) == 0 && hexSuffix(4)) return true;
    if (n.rfind("unk_", 0) == 0 && hexSuffix(4)) return true;
    if (n.rfind("j_",   0) == 0 && hexSuffix(2)) return true;
    return false;
}

GhidraCheckinPreview GhidraConnection::collectCheckinChanges(
    BinaryNinja::Ref<BinaryNinja::BinaryView> view) const
{
    GhidraCheckinPreview preview;
    if (!view || m_checkinItem.empty()) return preview;

    for (const auto& [bnAddr, key] : m_addrToKey) {
        auto origIt = m_addrToOriginalName.find(bnAddr);
        std::string origName = (origIt != m_addrToOriginalName.end()) ? origIt->second : "";
        auto sym = view->GetSymbolByAddress(bnAddr);
        if (!sym) continue;
        std::string curName = sym->GetFullName();
        if (curName == origName) continue;
        preview.renames.push_back({key, bnAddr, origName, curName});
    }

    // --- New symbols: user-named BN symbols at addresses Ghidra had no record for ---
    // We track seen addresses to avoid duplicates when GetSymbols() returns multiple
    // symbols at the same address (e.g. a function + its auto label).
    {
        std::unordered_set<uint64_t> seenAddrs;
        for (auto& sym : view->GetSymbols()) {
            uint64_t addr = sym->GetAddress();
            if (m_addrToKey.count(addr)) continue;   // covered by the rename loop above
            if (!seenAddrs.insert(addr).second) continue; // deduplicate
            BNSymbolType symType = sym->GetType();
            if (symType != FunctionSymbol &&
                symType != DataSymbol &&
                symType != LocalLabelSymbol) continue;
            std::string name = sym->GetFullName();
            if (name.empty() || isAutoGeneratedSymbolName(name)) continue;
            // Skip if unchanged since the last check-in of a new symbol at this address.
            auto baseIt = m_addrToOriginalName.find(addr);
            if (baseIt != m_addrToOriginalName.end() && baseIt->second == name) continue;
            preview.newSymbols.push_back({addr, name, symType == FunctionSymbol});
        }
    }

    for (uint64_t addr : view->GetCommentedAddresses()) {
        std::string text = view->GetCommentForAddress(addr);
        if (text.empty()) continue;

        auto origIt = m_addrToOriginalComment.find(addr);
        std::string origText = (origIt != m_addrToOriginalComment.end()) ? origIt->second : "";
        if (text == origText) continue;

        auto keyIt = m_addrToCommentKey.find(addr);
        std::string encodedKey = (keyIt != m_addrToCommentKey.end()) ? keyIt->second : std::string{};
        preview.comments.push_back({addr, text, encodedKey});
    }

    // Also collect function-level comments (added via BN's function comment UI or ';' at entry).
    for (auto& func : view->GetAnalysisFunctionList()) {
        uint64_t addr = func->GetStart();
        std::string text = func->GetComment();
        if (text.empty()) continue;

        // Skip if already captured as address-level comment at this address.
        bool alreadyCaptured = false;
        for (const auto& c : preview.comments)
            if (c.addr == addr) { alreadyCaptured = true; break; }
        if (alreadyCaptured) continue;

        auto origIt = m_addrToOriginalFuncComment.find(addr);
        std::string origText = (origIt != m_addrToOriginalFuncComment.end()) ? origIt->second : "";
        if (text == origText) continue;

        auto keyIt = m_addrToCommentKey.find(addr);
        std::string encodedKey = (keyIt != m_addrToCommentKey.end()) ? keyIt->second : std::string{};
        preview.comments.push_back({addr, text, encodedKey});
    }

    // Collect data type changes: new types and modified types.
    // We compare BN's current type library against the Ghidra baseline captured at checkout.
    auto bnTypes = view->GetTypes();
    for (auto& [qualName, bnType] : bnTypes) {
        std::string name = qualName.GetString();
        if (name.empty() || name.rfind("__", 0) == 0) continue;

        BNTypeClass cls = bnType->GetClass();
        if (cls != StructureTypeClass && cls != EnumerationTypeClass &&
            !(cls == NamedTypeReferenceClass && bnType->IsTypedefReference()))
            continue;  // only structs/unions/enums/typedefs

        GhidraCheckinPreview::DataTypeChange change;
        change.name = name;

        if (cls == StructureTypeClass) {
            auto structure = bnType->GetStructure();
            bool isUnion = (structure->GetStructureType() == UnionStructureType);
            change.kind = isUnion ? "union" : "struct";
            change.size = (int)bnType->GetWidth();
            for (auto& m : structure->GetMembers()) {
                GhidraCheckinPreview::DataTypeChange::Member mem;
                mem.name     = m.name;
                mem.offset   = (int)m.offset;
                mem.size     = (int)m.type->GetWidth();
                mem.typeName = m.type->GetString();
                change.members.push_back(std::move(mem));
            }
            if (change.members.empty()) continue; // skip empty placeholders
        } else if (cls == EnumerationTypeClass) {
            change.kind = "enum";
            change.size = (int)bnType->GetWidth();
            for (auto& v : bnType->GetEnumeration()->GetMembers()) {
                GhidraCheckinPreview::DataTypeChange::Value val;
                val.name  = v.name;
                val.value = (int64_t)v.value;
                change.values.push_back(std::move(val));
            }
            if (change.values.empty()) continue;
        } else { // NamedTypeReferenceClass + IsTypedefReference
            change.kind = "typedef";
            change.size = (int)bnType->GetWidth();
            auto ntr = bnType->GetNamedTypeReference();
            if (ntr) change.underlyingTypeName = ntr->GetName().GetString();
            if (change.underlyingTypeName.empty()) continue; // nothing useful
        }

        auto baseIt = m_ghidraTypesByName.find(name);
        if (baseIt == m_ghidraTypesByName.end()) {
            // Entirely new type created in BN after checkout
            change.op        = "add";
            change.ghidraId  = 0;
            preview.dataTypeChanges.push_back(std::move(change));
        } else {
            // Exists in Ghidra baseline — check if it was modified
            const GhidraDataType& base = baseIt->second;
            bool different = false;
            if (cls == StructureTypeClass) {
                different = (change.members.size() != base.members.size());
                if (!different) {
                    for (size_t i = 0; i < change.members.size() && !different; ++i)
                        different = (change.members[i].name     != base.members[i].name     ||
                                     change.members[i].offset   != base.members[i].offset   ||
                                     change.members[i].typeName != base.members[i].typeName);
                }
            } else if (cls == EnumerationTypeClass) {
                different = (change.values.size() != base.values.size());
                if (!different) {
                    for (size_t i = 0; i < change.values.size() && !different; ++i)
                        different = (change.values[i].name != base.values[i].name ||
                                     change.values[i].value != base.values[i].value);
                }
            } else { // typedef
                different = (change.underlyingTypeName != base.underlyingTypeName);
            }
            if (different) {
                change.op       = "update";
                change.ghidraId = base.id;
                preview.dataTypeChanges.push_back(std::move(change));
            }
        }
    }

    // Deletions: types that were in the Ghidra baseline but are no longer in BN.
    // Build a set of currently-known BN type names for quick lookup.
    {
        std::unordered_set<std::string> currentBnTypeNames;
        for (auto& [qn, _] : bnTypes)
            currentBnTypeNames.insert(qn.GetString());

        for (const auto& [name, baseType] : m_ghidraTypesByName) {
            if (currentBnTypeNames.count(name)) continue; // still present
            // Skip if we just queued an add/update for this name above (shouldn't happen,
            // but guard against double-processing).
            bool alreadyQueued = false;
            for (const auto& c : preview.dataTypeChanges)
                if (c.name == name) { alreadyQueued = true; break; }
            if (alreadyQueued) continue;

            GhidraCheckinPreview::DataTypeChange del;
            del.op       = "delete";
            del.kind     = baseType.kind;
            del.name     = name;
            del.ghidraId = baseType.id;
            preview.dataTypeChanges.push_back(std::move(del));
        }
    }

    // Signed rebase delta used for all addr conversions below.
    int64_t rebase = (int64_t)view->GetStart() - (int64_t)m_imageBase;

    // -----------------------------------------------------------------------
    // Detect renamed parameters AND locals
    // -----------------------------------------------------------------------
    for (auto& func : view->GetAnalysisFunctionList()) {
        uint64_t bnAddr     = func->GetStart();
        uint64_t ghidraAddr = (uint64_t)((int64_t)bnAddr - rebase);

        auto addrIt = m_paramKeyByAddrOrdinal.find(ghidraAddr);
        if (addrIt == m_paramKeyByAddrOrdinal.end()) continue;

        // Helper lambda: compare one variable against its Ghidra baseline and
        // append to preview.paramRenames if name or type has changed.
        auto checkVar = [&](const BinaryNinja::Variable& var, int ordinalKey,
                            bool isAutoGen) {
            auto ordIt = addrIt->second.find(ordinalKey);
            if (ordIt == addrIt->second.end()) return;
            int64_t key = ordIt->second;

            auto origIt = m_paramOriginalNameByKey.find(key);
            std::string origName = (origIt != m_paramOriginalNameByKey.end())
                ? origIt->second : "";

            std::string curName = func->GetVariableName(var);

            std::string curTypeName;
            {
                auto vt = func->GetVariableType(var);
                if (vt.GetValue()) curTypeName = vt.GetValue()->GetString();
            }
            auto typeBaseIt = m_paramOriginalTypeByKey.find(key);
            std::string origTypeName = (typeBaseIt != m_paramOriginalTypeByKey.end())
                ? typeBaseIt->second : "";

            bool nameChanged = !curName.empty() && curName != origName && !isAutoGen;
            bool typeChanged = !curTypeName.empty() && curTypeName != origTypeName;

            if (nameChanged || typeChanged) {
                GhidraCheckinPreview::ParamRename pr;
                pr.key      = key;
                pr.name     = nameChanged ? curName : origName;
                pr.typeName = typeChanged ? curTypeName : "";
                preview.paramRenames.push_back(std::move(pr));
            }
        };

        // --- Parameters (ordinal = index into parameter list) ---
        auto paramVars = func->GetParameterVariables().GetValue();
        for (size_t i = 0; i < paramVars.size(); ++i) {
            const auto& var = paramVars[i];
            std::string curName = func->GetVariableName(var);
            bool autoGen = curName.empty() ||
                           curName.rfind("arg",    0) == 0 ||
                           curName.rfind("param_", 0) == 0;
            checkVar(var, (int)i, autoGen);
        }

        // --- Local variables (ordinal = stack offset stored in Variable::storage) ---
        // GetVariables() returns map<Variable, VariableNameAndType> for all vars.
        auto allVars = func->GetVariables();
        for (const auto& [var, nat] : allVars) {
            if (var.type != StackVariableSourceType) continue;
            bool autoGen = nat.name.empty() ||
                           nat.name.rfind("var_",   0) == 0 ||
                           nat.name.rfind("local_", 0) == 0;
            checkVar(var, (int)var.storage, autoGen);
        }
    }

    // -----------------------------------------------------------------------
    // Detect new parameters (user-named BN params at ordinals not in baseline)
    // -----------------------------------------------------------------------
    for (auto& func : view->GetAnalysisFunctionList()) {
        uint64_t bnAddr     = func->GetStart();
        uint64_t ghidraAddr = (uint64_t)((int64_t)bnAddr - rebase);

        auto paramVars = func->GetParameterVariables().GetValue();
        auto addrIt    = m_paramKeyByAddrOrdinal.find(ghidraAddr);

        for (size_t i = 0; i < paramVars.size(); ++i) {
            // Skip if this ordinal already has a Ghidra DB key (handled by rename loop).
            if (addrIt != m_paramKeyByAddrOrdinal.end() &&
                addrIt->second.count((int)i)) continue;

            const auto& var = paramVars[i];
            std::string curName = func->GetVariableName(var);
            bool autoGen = curName.empty() ||
                           curName.rfind("arg",    0) == 0 ||
                           curName.rfind("param_", 0) == 0;
            if (autoGen) continue;

            // Skip if unchanged since a previous check-in of this new param.
            auto newBaseIt = m_newParamBaseline.find(ghidraAddr);
            if (newBaseIt != m_newParamBaseline.end()) {
                auto ordIt = newBaseIt->second.find((int)i);
                if (ordIt != newBaseIt->second.end() && ordIt->second == curName) continue;
            }

            std::string curTypeName;
            auto vt = func->GetVariableType(var);
            if (vt.GetValue()) curTypeName = vt.GetValue()->GetString();

            preview.newParams.push_back({bnAddr, (int)i, curName, curTypeName, /*isLocal=*/false});
        }
    }

    // -----------------------------------------------------------------------
    // Detect new register-allocated locals (gap #4)
    // SyncEngine never imports Ghidra register locals into BN (ordinals differ between
    // BN and Ghidra register numbering), so there is no "rename existing" case here.
    // We only need to detect user-named register vars that BN's HLIL analysis found and
    // that have no Ghidra record at all, then create LOCAL_VAR (type=7) symbols for them.
    // -----------------------------------------------------------------------
    for (auto& func : view->GetAnalysisFunctionList()) {
        uint64_t bnAddr     = func->GetStart();
        uint64_t ghidraAddr = (uint64_t)((int64_t)bnAddr - rebase);

        auto allVars = func->GetVariables();
        auto regBaseIt = m_newRegLocalBaseline.find(ghidraAddr);

        for (const auto& [var, nat] : allVars) {
            if (var.type != RegisterVariableSourceType) continue;

            std::string curName = nat.name;
            // Filter auto-generated register-local names from BN's HLIL analysis.
            // "in_*" is MLIL's convention for values entering via a register (e.g. in_RAX).
            bool autoGen = curName.empty() ||
                           curName.rfind("var_",   0) == 0 ||
                           curName.rfind("local_", 0) == 0 ||
                           curName.rfind("in_",    0) == 0;
            if (autoGen) continue;

            // Skip if unchanged since a previous check-in of this register local.
            int regKey = (int)var.storage; // BN register index (unique per register in a function)
            if (regBaseIt != m_newRegLocalBaseline.end()) {
                auto ordIt = regBaseIt->second.find(regKey);
                if (ordIt != regBaseIt->second.end() && ordIt->second == curName) continue;
            }

            std::string curTypeName;
            auto vt = func->GetVariableType(var);
            if (vt.GetValue()) curTypeName = vt.GetValue()->GetString();

            preview.newParams.push_back({bnAddr, regKey, curName, curTypeName, /*isLocal=*/true});
        }
    }

    // -----------------------------------------------------------------------
    // Detect bookmark changes (BN data tags with "Ghidra: " prefix)
    // -----------------------------------------------------------------------
    {
        std::unordered_set<BookmarkKey, BookmarkKeyHash> currentKeys;
        auto tagRefs = view->GetUserDataTagReferences();
        for (const auto& ref : tagRefs) {
            if (!ref.tag) continue;
            auto tt = ref.tag->GetType();
            if (!tt) continue;
            std::string typeName = tt->GetName();
            if (typeName.rfind("Ghidra: ", 0) != 0) continue;
            uint64_t ghidraAddr = (uint64_t)((int64_t)ref.addr - rebase);
            currentKeys.insert({ghidraAddr, typeName, ref.tag->GetData()});
        }

        // Tags in current but not baseline → add
        for (const auto& k : currentKeys) {
            if (m_ghidraBookmarkKeys.find(k) == m_ghidraBookmarkKeys.end()) {
                std::string typeName = k.tagTypeName.substr(8); // strip "Ghidra: "
                std::string category, comment;
                if (!k.tagData.empty() && k.tagData[0] == '[') {
                    auto close = k.tagData.find(']');
                    if (close != std::string::npos) {
                        category = k.tagData.substr(1, close - 1);
                        comment  = (k.tagData.size() > close + 2) ? k.tagData.substr(close + 2) : "";
                    }
                } else {
                    comment = k.tagData;
                }
                uint64_t bnAddr = (uint64_t)((int64_t)k.addr + rebase);
                preview.bookmarkChanges.push_back({"add", typeName, bnAddr, category, comment});
            }
        }
        // Tags in baseline but not current → delete
        for (const auto& k : m_ghidraBookmarkKeys) {
            if (currentKeys.find(k) == currentKeys.end()) {
                std::string typeName = k.tagTypeName.substr(8);
                uint64_t bnAddr = (uint64_t)((int64_t)k.addr + rebase);
                preview.bookmarkChanges.push_back({"delete", typeName, bnAddr, "", ""});
            }
        }
    }

    // -----------------------------------------------------------------------
    // Detect data variable additions and deletions
    // -----------------------------------------------------------------------
    {
        auto dataVars = view->GetDataVariables();

        // Additions: user-defined data vars in BN that weren't in Ghidra at checkout
        for (const auto& [bnAddr, dv] : dataVars) {
            if (dv.autoDiscovered) continue;
            uint64_t ghidraAddr = (uint64_t)((int64_t)bnAddr - rebase);
            if (m_ghidraDataItemAddrs.count(ghidraAddr)) continue;
            std::string typeName = dv.type.GetValue() ? dv.type.GetValue()->GetString() : "";
            if (typeName.empty()) continue;
            preview.dataItemChanges.push_back({"add", bnAddr, typeName});
        }

        // Deletions: data items that were in Ghidra at checkout but are now gone from BN
        for (uint64_t ghidraAddr : m_ghidraDataItemAddrs) {
            uint64_t bnAddr = (uint64_t)((int64_t)ghidraAddr + rebase);
            auto it = dataVars.find(bnAddr);
            bool stillPresent = (it != dataVars.end() && !it->second.autoDiscovered);
            if (!stillPresent)
                preview.dataItemChanges.push_back({"delete", bnAddr, ""});
        }
    }

    // -----------------------------------------------------------------------
    // Detect function signature changes (CC + return type)
    // -----------------------------------------------------------------------
    {
        // Build key → bnAddr inverse from m_addrToKey
        std::unordered_map<int64_t, uint64_t> keyToBnAddr;
        for (const auto& [addr, key] : m_addrToKey)
            keyToBnAddr[key] = addr;

        // Check functions where Ghidra had a non-default CC
        for (const auto& [key, origCC] : m_funcOriginalCC) {
            auto addrIt = keyToBnAddr.find(key);
            if (addrIt == keyToBnAddr.end()) continue;
            uint64_t bnAddr = addrIt->second;

            BinaryNinja::Ref<BinaryNinja::Function> func;
            {
                auto candidates = view->GetAnalysisFunctionsForAddress(bnAddr);
                for (auto& f : candidates)
                    if (f->GetStart() == bnAddr) { func = f; break; }
            }
            if (!func || !func->HasUserType()) continue;

            GhidraCheckinPreview::FuncSigChange change;
            change.key = key;

            // Calling convention
            auto bnCC = func->GetCallingConvention();
            std::string curCC = (bnCC.GetValue()) ? bnCC.GetValue()->GetName() : "";
            if (!curCC.empty() && curCC != origCC)
                change.callingConvention = curCC;

            // Return type
            auto retTypeIt = m_funcOriginalRetType.find(key);
            if (retTypeIt != m_funcOriginalRetType.end()) {
                auto bnRet = func->GetReturnType();
                std::string curRetType = bnRet.GetValue() ? bnRet.GetValue()->GetString() : "";
                if (!curRetType.empty() && curRetType != retTypeIt->second)
                    change.returnTypeName = curRetType;
            }

            if (!change.callingConvention.empty() || !change.returnTypeName.empty())
                preview.funcSigChanges.push_back(std::move(change));
        }

        // Also catch functions where the user explicitly typed a signature in BN but
        // Ghidra had no CC / return type baseline (or the function wasn't captured in the
        // first loop because its CC matched the default).  Any function with HasUserType()
        // and a key in m_addrToKey is a candidate.
        for (auto& func : view->GetAnalysisFunctionList()) {
            if (!func->HasUserType()) continue;
            uint64_t bnAddr = func->GetStart();

            auto keyIt = m_addrToKey.find(bnAddr);
            if (keyIt == m_addrToKey.end()) continue;
            int64_t key = keyIt->second;

            // Skip if already captured by the first loop above
            bool alreadyCaptured = false;
            for (const auto& c : preview.funcSigChanges)
                if (c.key == key) { alreadyCaptured = true; break; }
            if (alreadyCaptured) continue;

            GhidraCheckinPreview::FuncSigChange change;
            change.key = key;

            auto bnCC = func->GetCallingConvention();
            if (bnCC.GetValue()) {
                std::string curCC = bnCC.GetValue()->GetName();
                auto origIt = m_funcOriginalCC.find(key);
                if (origIt == m_funcOriginalCC.end() || curCC != origIt->second)
                    change.callingConvention = curCC;
            }

            auto bnRet = func->GetReturnType();
            if (bnRet.GetValue()) {
                std::string curRet = bnRet.GetValue()->GetString();
                auto origIt = m_funcOriginalRetType.find(key);
                if (origIt == m_funcOriginalRetType.end() || curRet != origIt->second)
                    change.returnTypeName = curRet;
            }

            if (!change.callingConvention.empty() || !change.returnTypeName.empty())
                preview.funcSigChanges.push_back(std::move(change));
        }
    }

    // -----------------------------------------------------------------------
    // Detect equate renames: if the user edited the BN enum type's member label.
    // -----------------------------------------------------------------------
    for (const auto& [id, originalName] : m_equateOriginalNameById) {
        std::string typeId = "ghidra_eq_" + std::to_string(id);
        BinaryNinja::Ref<BinaryNinja::Type> bnType = view->GetTypeById(typeId);
        if (!bnType) continue;
        auto enumeration = bnType->GetEnumeration();
        if (!enumeration) continue;
        auto members = enumeration->GetMembers();
        if (members.empty()) continue;

        std::string currentLabel = members[0].name;
        if (currentLabel == originalName) continue;

        preview.equateRenames.push_back({id, currentLabel});
    }

    // -----------------------------------------------------------------------
    // Detect new equate bindings (addr+opIndex pairs the user applied in BN
    // that were not in the Ghidra ref set at checkout).
    // -----------------------------------------------------------------------
    for (const auto& [id, originalName] : m_equateOriginalNameById) {
        std::string typeId = "ghidra_eq_" + std::to_string(id);

        // Get our equate's BN enum type by its stable ID.
        BinaryNinja::Ref<BinaryNinja::Type> bnType = view->GetTypeById(typeId);
        if (!bnType) continue;

        int64_t value = 0;
        {
            auto valIt = m_equateValueById.find(id);
            if (valIt != m_equateValueById.end()) value = valIt->second;
        }

        // GetCodeReferencesForType returns all instruction addresses that reference
        // this named enum type (including integer display type overrides set by SyncEngine
        // or the user).
        BinaryNinja::QualifiedName qualName(originalName);
        auto refs = view->GetCodeReferencesForType(qualName);

        auto baseIt = m_equateRefsByEquateId.find(id);

        for (const auto& ref : refs) {
            if (!ref.func || !ref.arch) continue;
            uint64_t bnAddr    = ref.addr;
            uint64_t ghidraAddr = (uint64_t)((int64_t)bnAddr - rebase);

            // Check baseline first — if ALL operands at this address are in baseline,
            // skip the expensive per-operand probe.
            bool allInBaseline = true;
            if (baseIt == m_equateRefsByEquateId.end()) {
                allInBaseline = false;
            } else {
                // We'll verify per-operand below; just note that the address might be new.
                // (Can't shortcut here without knowing which opIndex(es) are applied.)
                allInBaseline = false; // always probe; baseline check is per-operand
            }
            (void)allInBaseline;

            // Probe operand indices 0..3 for this instruction.
            for (int opIdx = 0; opIdx < 4; ++opIdx) {
                BNIntegerDisplayType dtype = ref.func->GetIntegerConstantDisplayType(
                    ref.arch, bnAddr, (uint64_t)value, (size_t)opIdx);
                if (dtype != EnumerationDisplayType) continue;

                // Verify the enum type is specifically our equate (not some other enum
                // that happens to use the same value).
                BinaryNinja::Ref<BinaryNinja::Type> appliedType =
                    ref.func->GetIntegerConstantDisplayTypeEnumType(
                        ref.arch, bnAddr, (uint64_t)value, (size_t)opIdx);
                if (!appliedType) continue;
                auto ntr = appliedType->GetRegisteredName();
                if (!ntr || ntr->GetTypeId() != typeId) continue;

                // Skip if this (ghidraAddr, opIndex) was in the baseline.
                if (baseIt != m_equateRefsByEquateId.end() &&
                    baseIt->second.count({ghidraAddr, opIdx})) continue;

                preview.newEquateRefs.push_back({id, bnAddr, opIdx});
            }
        }
    }

    return preview;
}

bool GhidraConnection::checkin(BinaryNinja::Ref<BinaryNinja::BinaryView> view,
                                const GhidraCheckinPreview& preview,
                                const std::string& comment,
                                std::string& errorOut)
{
    if (!isBridgeConnected())  { errorOut = "Bridge not connected";    return false; }
    if (!m_serverConnected)    { errorOut = "Not connected to server"; return false; }
    if (m_checkinItem.empty()) { errorOut = "No import — import first"; return false; }

    // Compute the rebase so the bridge can encode new-comment / new-symbol addresses.
    uint64_t bnBase    = view->GetStart();
    int64_t  rebase    = (int64_t)bnBase - (int64_t)m_imageBase;

    nlohmann::json symbols = nlohmann::json::array();
    for (const auto& r : preview.renames)
        symbols.push_back({{"key", r.key}, {"name", r.newName}});
    // New symbols: no Ghidra key — send VA so the bridge can find/create a record.
    for (const auto& s : preview.newSymbols) {
        uint64_t ghidraVa = (uint64_t)((int64_t)s.addr - rebase);
        symbols.push_back({
            {"va",       toHex(ghidraVa)},
            {"name",     s.name},
            {"sym_type", s.isFunction ? 5 : 0}  // 5=FUNCTION (new), 0=LABEL
        });
    }

    nlohmann::json comments = nlohmann::json::array();
    for (const auto& c : preview.comments) {
        // Ghidra VA = BN addr − rebase.
        uint64_t ghidraVa = (uint64_t)((int64_t)c.addr - rebase);
        // Route to the correct Ghidra comment column:
        //   • plate  — originated as a function-header comment (imported via func->SetComment)
        //   • eol/pre/post/rep — whichever single column held the text originally;
        //                        merged multi-column text defaults back to "eol"
        nlohmann::json entry = {{"va", toHex(ghidraVa)}};
        std::string field;
        if (m_addrToOriginalFuncComment.count(c.addr)) {
            field = "plate";
        } else {
            auto fIt = m_addrToCommentField.find(c.addr);
            field = (fIt != m_addrToCommentField.end()) ? fIt->second : "eol";
        }
        entry[field] = c.text;
        if (!c.encodedKey.empty())
            entry["key"] = c.encodedKey;
        comments.push_back(std::move(entry));
    }

    nlohmann::json equateRenames = nlohmann::json::array();
    for (const auto& r : preview.equateRenames)
        equateRenames.push_back({{"id", r.id}, {"name", r.name}});

    nlohmann::json equateRefAdds = nlohmann::json::array();
    for (const auto& r : preview.newEquateRefs) {
        uint64_t ghidraVa = (uint64_t)((int64_t)r.addr - rebase);
        equateRefAdds.push_back({
            {"id",       r.equateId},
            {"va",       toHex(ghidraVa)},
            {"op_index", r.opIndex}
        });
    }

    nlohmann::json bookmarkChanges = nlohmann::json::array();
    for (const auto& b : preview.bookmarkChanges) {
        uint64_t ghidraVa = (uint64_t)((int64_t)b.addr - (int64_t)(view->GetStart() - m_imageBase));
        bookmarkChanges.push_back({
            {"op",       b.op},
            {"type",     b.type},
            {"addr",     toHex(ghidraVa)},
            {"category", b.category},
            {"comment",  b.comment}
        });
    }

    nlohmann::json paramRenames = nlohmann::json::array();
    for (const auto& r : preview.paramRenames) {
        nlohmann::json j = {{"key", r.key}, {"name", r.name}};
        if (!r.typeName.empty()) j["type_name"] = r.typeName;
        paramRenames.push_back(j);
    }
    // New params/reg-locals: no Ghidra key — send func_va + ordinal so the bridge can create a record.
    for (const auto& p : preview.newParams) {
        uint64_t ghidraVa = (uint64_t)((int64_t)p.funcAddr - rebase);
        nlohmann::json j = {
            {"func_va",  toHex(ghidraVa)},
            {"ordinal",  p.ordinal},
            {"name",     p.name},
            {"is_local", p.isLocal}   // true → LOCAL_VAR (type=7), false → PARAMETER (type=6)
        };
        if (!p.typeName.empty()) j["type_name"] = p.typeName;
        paramRenames.push_back(j);
    }

    nlohmann::json dataTypeChanges = nlohmann::json::array();
    for (const auto& dt : preview.dataTypeChanges) {
        nlohmann::json j;
        j["op"]        = dt.op;
        j["kind"]      = dt.kind;
        j["name"]      = dt.name;
        j["ghidra_id"] = dt.ghidraId;
        j["size"]      = dt.size;
        if (!dt.members.empty()) {
            nlohmann::json members = nlohmann::json::array();
            for (const auto& m : dt.members) {
                members.push_back({{"name", m.name}, {"offset", m.offset},
                                   {"size", m.size}, {"type_name", m.typeName}});
            }
            j["members"] = members;
        }
        if (!dt.values.empty()) {
            nlohmann::json values = nlohmann::json::array();
            for (const auto& v : dt.values) {
                values.push_back({{"name", v.name}, {"value", v.value}});
            }
            j["values"] = values;
        }
        if (!dt.underlyingTypeName.empty())
            j["underlying_type_name"] = dt.underlyingTypeName;
        dataTypeChanges.push_back(j);
    }

    nlohmann::json dataItemChanges = nlohmann::json::array();
    for (const auto& di : preview.dataItemChanges) {
        // Convert BN VA → Ghidra VA before sending (same convention as bookmarks/comments).
        uint64_t ghidraItemVa = (uint64_t)((int64_t)di.addr - rebase);
        dataItemChanges.push_back({
            {"op",        di.op},
            {"addr",      toHex(ghidraItemVa)},
            {"type_name", di.typeName}
        });
    }

    nlohmann::json funcSigChanges = nlohmann::json::array();
    for (const auto& fs : preview.funcSigChanges) {
        nlohmann::json j;
        j["key"] = fs.key;
        if (!fs.callingConvention.empty()) j["cc"]       = fs.callingConvention;
        if (!fs.returnTypeName.empty())     j["ret_type"] = fs.returnTypeName;
        funcSigChanges.push_back(j);
    }

    BinaryNinja::LogInfo("ghidra-bridge: checkin %s — %d renamed, %d new syms, %d comments, %d equate renames, %d new eq refs, %d bookmarks, %d params, %d new params, %d datatypes, %d dataitems, %d funcsigs",
        m_checkinItem.c_str(), (int)preview.renames.size(), (int)preview.newSymbols.size(),
        (int)comments.size(), (int)equateRenames.size(), (int)equateRefAdds.size(),
        (int)bookmarkChanges.size(), (int)preview.paramRenames.size(), (int)preview.newParams.size(),
        (int)dataTypeChanges.size(), (int)dataItemChanges.size(), (int)funcSigChanges.size());

    try {
        auto resp = m_client->sendSync({
            {"op",                "checkin"},
            {"repo",              m_checkinRepo},
            {"folder",            m_checkinFolder},
            {"item",              m_checkinItem},
            {"comment",           comment},
            {"symbols",           symbols},
            {"comments",          comments},
            {"equate_renames",    equateRenames},
            {"equate_ref_adds",   equateRefAdds},
            {"bookmark_changes",  bookmarkChanges},
            {"param_renames",     paramRenames},
            {"data_type_changes", dataTypeChanges},
            {"data_item_changes", dataItemChanges},
            {"func_sig_changes",  funcSigChanges}
        }, /*timeoutMs=*/120'000);

        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "checkin failed");
            return false;
        }

        // Record DB keys the bridge assigned to newly-added types (op=add, ghidraId was 0).
        // Without this, a subsequent re-modification would send op=update with id=0 and
        // fail to find the record, silently creating a duplicate.
        if (resp.contains("added_type_ids") && resp["added_type_ids"].is_object()) {
            for (auto& [typeName, idVal] : resp["added_type_ids"].items()) {
                if (!idVal.is_number()) continue;
                auto it = m_ghidraTypesByName.find(typeName);
                if (it != m_ghidraTypesByName.end())
                    it->second.id = idVal.get<int64_t>();
            }
        }

        // Update baselines so the next preview won't re-show the same changes.
        for (const auto& r : preview.renames)
            m_addrToOriginalName[r.addr] = r.newName;
        // Record new symbols so they're not re-queued on the next check-in.
        for (const auto& s : preview.newSymbols)
            m_addrToOriginalName[s.addr] = s.name;
        for (const auto& c : preview.comments) {
            // Update whichever map originally held this address.
            if (m_addrToOriginalComment.count(c.addr))
                m_addrToOriginalComment[c.addr] = c.text;
            else if (m_addrToOriginalFuncComment.count(c.addr))
                m_addrToOriginalFuncComment[c.addr] = c.text;
            else {
                m_addrToOriginalComment[c.addr] = c.text; // new comment, treat as address-level
                m_addrToCommentField[c.addr] = "eol";     // new BN comment defaults to EOL column
            }
        }
        for (const auto& dt : preview.dataTypeChanges) {
            if (dt.op == "delete") {
                m_ghidraTypesByName.erase(dt.name);
                continue;
            }
            // Add or update baseline entry
            GhidraDataType& stored = m_ghidraTypesByName[dt.name];
            stored.name = dt.name;
            stored.kind = dt.kind;
            stored.size = dt.size;
            if (dt.ghidraId != 0) stored.id = dt.ghidraId;
            stored.underlyingTypeName = dt.underlyingTypeName;
            stored.members.clear();
            for (const auto& m : dt.members) {
                GhidraDataTypeMember mem;
                mem.name   = m.name;
                mem.offset = m.offset;
                mem.size   = m.size;
                stored.members.push_back(mem);
            }
            stored.values.clear();
            for (const auto& v : dt.values) {
                GhidraEnumValue ev;
                ev.name  = v.name;
                ev.value = v.value;
                stored.values.push_back(ev);
            }
        }

        // Update param baseline
        for (const auto& r : preview.paramRenames) {
            m_paramOriginalNameByKey[r.key] = r.name;
            if (!r.typeName.empty()) m_paramOriginalTypeByKey[r.key] = r.typeName;
        }
        // Record new params/reg-locals so they're not re-queued on the next check-in.
        {
            int64_t rebaseLocal = (int64_t)view->GetStart() - (int64_t)m_imageBase;
            for (const auto& p : preview.newParams) {
                uint64_t ghidraAddr = (uint64_t)((int64_t)p.funcAddr - rebaseLocal);
                if (p.isLocal)
                    m_newRegLocalBaseline[ghidraAddr][p.ordinal] = p.name; // ordinal = BN reg index
                else
                    m_newParamBaseline[ghidraAddr][p.ordinal] = p.name;
            }
        }

        // Update equate name baseline
        for (const auto& r : preview.equateRenames)
            m_equateOriginalNameById[r.id] = r.name;

        // Update equate ref baseline so new bindings aren't re-queued next check-in.
        for (const auto& r : preview.newEquateRefs) {
            uint64_t ghidraAddr = (uint64_t)((int64_t)r.addr - rebase);
            m_equateRefsByEquateId[r.equateId].insert({ghidraAddr, r.opIndex});
        }

        // Update bookmark baseline
        int64_t rebase2 = (int64_t)view->GetStart() - (int64_t)m_imageBase;
        for (const auto& b : preview.bookmarkChanges) {
            std::string tagTypeName = "Ghidra: " + b.type;
            std::string tagData;
            if (!b.category.empty()) tagData += "[" + b.category + "] ";
            tagData += b.comment;
            uint64_t ghidraAddr = (uint64_t)((int64_t)b.addr - rebase2);
            BookmarkKey k{ghidraAddr, tagTypeName, tagData};
            if (b.op == "add")
                m_ghidraBookmarkKeys.insert(k);
            else
                m_ghidraBookmarkKeys.erase(k);
        }

        // Update data-item baseline
        for (const auto& di : preview.dataItemChanges) {
            uint64_t ghidraAddr = (uint64_t)((int64_t)di.addr - rebase2);
            if (di.op == "add")    m_ghidraDataItemAddrs.insert(ghidraAddr);
            else                   m_ghidraDataItemAddrs.erase(ghidraAddr);
        }

        // Update function signature baseline
        for (const auto& fs : preview.funcSigChanges) {
            if (!fs.callingConvention.empty()) m_funcOriginalCC[fs.key]      = fs.callingConvention;
            if (!fs.returnTypeName.empty())     m_funcOriginalRetType[fs.key] = fs.returnTypeName;
        }

        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

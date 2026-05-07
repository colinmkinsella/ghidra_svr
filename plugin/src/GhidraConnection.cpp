#include "GhidraConnection.h"
#include <binaryninjaapi.h>
#ifdef _WIN32
#  include <windows.h>
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
// Bridge lifecycle
// ---------------------------------------------------------------------------

static std::string autoDetectBridgeJar() {
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
    if (slash != std::string::npos)
        return path.substr(0, slash + 1) + "ghidra-bridge-0.1.0.jar";
#endif
    return {};
}

bool GhidraConnection::startBridge(std::string& errorOut) {
    if (m_process && m_process->isRunning()) return true;

    std::string jar = bridgeJar();
    if (jar.empty()) jar = autoDetectBridgeJar();
    if (jar.empty()) {
        errorOut = "Bridge JAR not found. Set ghidra.bridgeJar in settings.";
        return false;
    }

    m_process = std::make_unique<BridgeProcess>();
    if (!m_process->start(javaExe(), jar, ghidraHome(), trustAll(), errorOut)) {
        m_process.reset();
        return false;
    }

    m_client = std::make_unique<BridgeClient>();
    if (!m_client->connect(m_process->port(), errorOut)) {
        m_process->stop();
        m_process.reset();
        m_client.reset();
        return false;
    }

    m_client->setEventCallback([this](nlohmann::json evt) {
        onBridgeEvent(std::move(evt));
    });

    return true;
}

void GhidraConnection::stopBridge() {
    disconnectFromServer();
    if (m_client) { m_client->disconnect(); m_client.reset(); }
    if (m_process){ m_process->stop();      m_process.reset(); }
}

bool GhidraConnection::isBridgeRunning() const {
    return m_process && m_process->isRunning() &&
           m_client  && m_client->isConnected();
}

// ---------------------------------------------------------------------------
// Server connection
// ---------------------------------------------------------------------------

bool GhidraConnection::connectToServer(const std::string& host, int port,
                                        const std::string& user,
                                        const std::string& password,
                                        std::string& errorOut) {
    if (!isBridgeRunning()) {
        if (!startBridge(errorOut)) return false;
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
        m_user = user;
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

void GhidraConnection::disconnectFromServer() {
    if (!m_client || !m_serverConnected) return;
    try {
        m_client->sendSync({{"op", "disconnect"}}, 3000);
    } catch (...) {}
    m_serverConnected = false;
    m_user.clear();
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
            out.funcFlags.push_back(std::move(f));
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
    m_imageBase                 = imageBase;
    m_checkinRepo               = repo;
    m_checkinFolder             = folder;
    m_checkinItem               = item;
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

    return preview;
}

bool GhidraConnection::checkin(BinaryNinja::Ref<BinaryNinja::BinaryView> view,
                                const GhidraCheckinPreview& preview,
                                const std::string& comment,
                                std::string& errorOut)
{
    if (!isBridgeRunning())    { errorOut = "Bridge not running";      return false; }
    if (!m_serverConnected)    { errorOut = "Not connected to server"; return false; }
    if (m_checkinItem.empty()) { errorOut = "No import — import first"; return false; }

    nlohmann::json symbols = nlohmann::json::array();
    for (const auto& r : preview.renames)
        symbols.push_back({{"key", r.key}, {"name", r.newName}});

    // Compute the rebase so the bridge can encode new-comment addresses.
    uint64_t bnBase    = view->GetStart();
    int64_t  rebase    = (int64_t)bnBase - (int64_t)m_imageBase;

    nlohmann::json comments = nlohmann::json::array();
    for (const auto& c : preview.comments) {
        // Ghidra VA = BN addr − rebase.
        uint64_t ghidraVa = (uint64_t)((int64_t)c.addr - rebase);
        nlohmann::json entry = {
            {"va",  toHex(ghidraVa)},
            {"eol", c.text}
        };
        if (!c.encodedKey.empty())
            entry["key"] = c.encodedKey;
        comments.push_back(std::move(entry));
    }

    BinaryNinja::LogInfo("ghidra-bridge: checkin %s — %d renamed, %d comments",
        m_checkinItem.c_str(), (int)symbols.size(), (int)comments.size());

    try {
        auto resp = m_client->sendSync({
            {"op",      "checkin"},
            {"repo",    m_checkinRepo},
            {"folder",  m_checkinFolder},
            {"item",    m_checkinItem},
            {"comment", comment},
            {"symbols",  symbols},
            {"comments", comments}
        }, /*timeoutMs=*/120'000);

        if (!resp.value("ok", false)) {
            errorOut = resp.value("error", "checkin failed");
            return false;
        }

        // Update baselines so the next preview won't re-show the same changes.
        for (const auto& r : preview.renames)
            m_addrToOriginalName[r.addr] = r.newName;
        for (const auto& c : preview.comments) {
            // Update whichever map originally held this address.
            if (m_addrToOriginalComment.count(c.addr))
                m_addrToOriginalComment[c.addr] = c.text;
            else if (m_addrToOriginalFuncComment.count(c.addr))
                m_addrToOriginalFuncComment[c.addr] = c.text;
            else
                m_addrToOriginalComment[c.addr] = c.text; // new comment, treat as address-level
        }
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

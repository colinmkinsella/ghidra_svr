#pragma once
#include "BridgeProcess.h"
#include "BridgeClient.h"
#include <binaryninjaapi.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Data transfer objects (mirror the bridge JSON shapes)
// ---------------------------------------------------------------------------

struct RepoItem {
    std::string name;
    std::string parentPath;
    std::string path;
    std::string fileId;
    int         itemType    = 0; // 1=FILE, 2=DATABASE, 3=TEXT_DATA_FILE
    std::string contentType;
    int         version     = -1;
    int64_t     versionTime = 0;
};

struct CheckoutInfo {
    int64_t     id          = 0;
    std::string type;        // "NORMAL" | "EXCLUSIVE" | "TRANSIENT"
    std::string user;
    int         version     = -1;
    int64_t     time        = 0;
    std::string projectPath;
};

struct VersionInfo {
    int         version     = -1;
    int64_t     time        = 0;
    std::string user;
    std::string comment;
};

// ---------------------------------------------------------------------------
// Database export — returned by openDatabase()
// ---------------------------------------------------------------------------

/** Symbol types matching Ghidra's SymbolType ordinals. */
enum class GhidraSymbolType : int {
    Label     = 0,
    Library   = 1,
    Namespace = 2,
    Class     = 3,
    Function  = 4,
    Parameter = 5,
    LocalVar  = 6,
    GlobalVar = 7,
    External  = 8,
};

/** Symbol source — 0=DEFAULT (auto), 1=ANALYSIS, 2=IMPORTED, 3=USER_DEFINED */
enum class GhidraSymbolSource : int {
    Default  = 0,
    Analysis = 1,
    Imported = 2,
    User     = 3,
};

struct GhidraSymbol {
    int64_t          key         = 0;
    std::string      name;
    uint64_t         addr        = 0;
    GhidraSymbolType type        = GhidraSymbolType::Label;
    GhidraSymbolSource source    = GhidraSymbolSource::Default;
    int64_t          namespaceId = 0;
};

struct GhidraComment {
    uint64_t    addr = 0;
    std::string eol, pre, post, plate, rep;
    std::string encodedKey; // "0x..." encoded Ghidra address — used for write-back
};

struct GhidraFuncFlags {
    int64_t key     = 0;
    bool    thunk   = false;
    bool    noReturn= false;
    bool    isInline= false;
};

/** Changes collected from the BN view that are pending check-in. */
struct GhidraCheckinPreview {
    struct SymbolRename {
        int64_t     key;
        uint64_t    addr;
        std::string originalName;
        std::string newName;
    };
    struct CommentChange {
        uint64_t    addr;
        std::string text;
        std::string encodedKey;
    };
    std::vector<SymbolRename>  renames;
    std::vector<CommentChange> comments;

    bool empty() const { return renames.empty() && comments.empty(); }
};

/** Everything extracted from one Ghidra program database. */
struct GhidraDbExport {
    std::vector<GhidraSymbol>    symbols;
    std::vector<GhidraComment>   comments;
    std::vector<GhidraFuncFlags> funcFlags;
    std::vector<std::string>     diag;      // diagnostic lines from the bridge
    uint64_t                     imageBase = 0; // Ghidra segment-0 base VA
};

struct GhidraEvent {
    std::string repo;
    std::string type;         // "REP_ITEM_CHANGED" etc.
    std::string parentPath;
    std::string name;
    std::string newParentPath;
    std::string newName;
};

// ---------------------------------------------------------------------------
// GhidraConnection — singleton owning the bridge process and client
// ---------------------------------------------------------------------------

/**
 * High-level API for the Ghidra server connection.
 *
 * Owns the bridge process (Java) and the TCP client.  All methods that
 * contact the server block the calling thread — run them from BN
 * background tasks, not from the main/UI thread.
 *
 * Event callback is invoked from the BridgeClient receive thread.
 */
class GhidraConnection {
public:
    static GhidraConnection& instance();

    GhidraConnection(const GhidraConnection&) = delete;
    GhidraConnection& operator=(const GhidraConnection&) = delete;

    // -----------------------------------------------------------------------
    // Bridge lifecycle
    // -----------------------------------------------------------------------

    /**
     * Start the bridge process.  Call once at plugin init (or lazily on
     * first connect).  Safe to call if already started.
     */
    bool startBridge(std::string& errorOut);
    void stopBridge();
    bool isBridgeRunning() const;

    // -----------------------------------------------------------------------
    // Server connection
    // -----------------------------------------------------------------------

    bool connectToServer(const std::string& host, int port,
                         const std::string& user, const std::string& password,
                         std::string& errorOut);
    void disconnectFromServer();
    bool isServerConnected() const { return m_serverConnected; }
    const std::string& connectedUser() const { return m_user; }

    // -----------------------------------------------------------------------
    // Repository operations  (all block; call from background thread)
    // -----------------------------------------------------------------------

    std::vector<std::string> listRepos(std::string& errorOut);

    bool openRepo(const std::string& repo, std::string& errorOut);
    void closeRepo(const std::string& repo);

    std::vector<RepoItem>   listItems(const std::string& repo,
                                       const std::string& folder,
                                       std::string& errorOut);
    std::vector<std::string> getSubfolders(const std::string& repo,
                                            const std::string& folder,
                                            std::string& errorOut);
    std::vector<VersionInfo> getVersions(const std::string& repo,
                                          const std::string& folder,
                                          const std::string& item,
                                          std::string& errorOut);
    std::vector<CheckoutInfo> getCheckouts(const std::string& repo,
                                            const std::string& folder,
                                            const std::string& item,
                                            std::string& errorOut);

    CheckoutInfo checkout(const std::string& repo,
                          const std::string& folder,
                          const std::string& item,
                          const std::string& checkoutType, // "NORMAL"|"EXCLUSIVE"|"TRANSIENT"
                          std::string& errorOut);

    /**
     * Open a Ghidra database for read-only access and export its contents.
     *
     * The call blocks until the entire database has been read over the RMI
     * connection — for large programs (~100 K symbols) this can take 5–30 s.
     * Run from a BN background task, never from the UI thread.
     *
     * @param version  Version to open (-1 = latest).
     */
    GhidraDbExport openDatabase(const std::string& repo,
                                const std::string& folder,
                                const std::string& item,
                                int version,
                                std::string& errorOut);

    struct BinaryFile {
        std::string          filename;
        std::vector<uint8_t> bytes;
    };

    /** Download raw file bytes stored in the Ghidra database.  May return multiple
     *  entries if the program was imported from multiple source files. */
    std::vector<BinaryFile> downloadBinary(const std::string& repo,
                                           const std::string& folder,
                                           const std::string& item,
                                           int version,
                                           std::string& errorOut);

    bool terminateCheckout(const std::string& repo,
                           const std::string& folder,
                           const std::string& item,
                           int64_t checkoutId,
                           std::string& errorOut);

    // -----------------------------------------------------------------------
    // Write-back (check in BN annotations to Ghidra server)
    // -----------------------------------------------------------------------

    /**
     * Store per-address mappings gathered during the last import so that
     * checkin() can compute which symbols were renamed and which comments
     * need updating.  Call from the import background thread before the
     * UI callback fires.
     */
    void storeCheckinState(
        std::unordered_map<uint64_t, int64_t>     addrToKey,
        std::unordered_map<uint64_t, std::string> addrToOriginalName,
        std::unordered_map<uint64_t, std::string> addrToCommentKey,
        std::unordered_map<uint64_t, std::string> addrToOriginalComment,
        std::unordered_map<uint64_t, std::string> addrToOriginalFuncComment,
        uint64_t imageBase,
        const std::string& repo,
        const std::string& folder,
        const std::string& item);

    /**
     * Collect pending changes (renamed symbols, modified comments) from
     * @p view without contacting the server.  Fast enough to call on the
     * UI thread.  Returns an empty preview if nothing has changed.
     */
    GhidraCheckinPreview collectCheckinChanges(
        BinaryNinja::Ref<BinaryNinja::BinaryView> view) const;

    /**
     * Push the pre-collected @p preview back to the Ghidra server item
     * that was last imported.  Does an exclusive checkout internally.
     *
     * Blocks the calling thread — run from a BN background worker.
     */
    bool checkin(BinaryNinja::Ref<BinaryNinja::BinaryView> view,
                 const GhidraCheckinPreview& preview,
                 const std::string& comment,
                 std::string& errorOut);

    bool hasCheckinState() const { return !m_checkinItem.empty(); }
    bool isCheckinItem(const std::string& repo, const std::string& folder,
                       const std::string& item) const {
        return m_checkinRepo == repo && m_checkinFolder == folder && m_checkinItem == item;
    }
    const std::string& checkinRepo()   const { return m_checkinRepo; }
    const std::string& checkinFolder() const { return m_checkinFolder; }
    const std::string& checkinItem()   const { return m_checkinItem; }

    // -----------------------------------------------------------------------
    // Events
    // -----------------------------------------------------------------------

    using EventHandler = std::function<void(GhidraEvent)>;
    void setEventHandler(EventHandler h);

    // -----------------------------------------------------------------------
    // Settings
    // -----------------------------------------------------------------------

    std::string javaExe()   const;
    std::string bridgeJar() const;
    std::string ghidraHome() const;
    bool        trustAll()  const;

private:
    GhidraConnection() = default;

    std::unique_ptr<BridgeProcess> m_process;
    std::unique_ptr<BridgeClient>  m_client;
    bool        m_serverConnected = false;
    std::string m_user;

    EventHandler m_eventHandler;

    // Checkin state — populated by storeCheckinState() after each import.
    std::unordered_map<uint64_t, int64_t>     m_addrToKey;
    std::unordered_map<uint64_t, std::string> m_addrToOriginalName;
    std::unordered_map<uint64_t, std::string> m_addrToCommentKey;
    std::unordered_map<uint64_t, std::string> m_addrToOriginalComment;
    std::unordered_map<uint64_t, std::string> m_addrToOriginalFuncComment;
    uint64_t    m_imageBase   = 0;
    std::string m_checkinRepo, m_checkinFolder, m_checkinItem;

    void onBridgeEvent(nlohmann::json evt);

    // Helpers
    static RepoItem    itemFromJson(const nlohmann::json& j);
    static CheckoutInfo checkoutFromJson(const nlohmann::json& j);
    static VersionInfo  versionFromJson(const nlohmann::json& j);
};

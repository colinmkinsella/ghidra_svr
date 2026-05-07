package com.ghidra_svr.bridge;

import com.google.gson.*;
import ghidra.framework.remote.RepositoryChangeEvent;
import ghidra.framework.remote.RepositoryItem;
import ghidra.framework.store.ItemCheckoutStatus;
import ghidra.framework.store.Version;

import java.io.*;
import java.net.Socket;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Handles one TCP client (the Binary Ninja C++ plugin).
 *
 * Protocol: newline-delimited JSON in both directions.
 *
 * Every request carries an integer "id" and a string "op".  The bridge
 * always sends a response with the same "id".  Asynchronous events (pushed
 * by EventStreamers) carry an "event" key instead of "id".
 *
 * Request ops
 * -----------
 *  ping
 *  connect         host, port, user, password
 *  disconnect
 *  status
 *  list_repos
 *  open_repo       repo
 *  close_repo      repo
 *  list_items      repo, [folder="/"]
 *  get_subfolders  repo, [folder="/"]
 *  checkout        repo, [folder="/"], item, [type="NORMAL"], [project_path]
 *  terminate_checkout  repo, [folder="/"], item, checkout_id
 *  get_versions    repo, [folder="/"], item
 *  get_checkouts   repo, [folder="/"], item
 *  open_db         repo, [folder="/"], item, [version=-1]
 *  checkin         repo, [folder="/"], item, [comment], [symbols=[]], [comments=[]]
 *
 * Successful response shape
 * -------------------------
 *  { "id": <N>, "ok": true, ...extra fields... }
 *
 * Error response shape
 * --------------------
 *  { "id": <N>, "ok": false, "error": "<message>" }
 *
 * Async event shape
 * -----------------
 *  { "event": "repo_changed", "repo": "<name>",
 *    "type":  "<REP_*>",
 *    "parent_path": "...", "name": "...",
 *    "new_parent_path": "...", "new_name": "..." }
 */
public class BridgeConnection implements Runnable {

    private final Socket socket;
    private final GhidraSession session = new GhidraSession();
    private final Gson gson = new Gson();

    // Guarded by 'this' — both the request-handler thread and EventStreamer threads write here.
    private PrintWriter out;

    // EventStreamer per open repo.  Accessed only from the request-handler thread.
    private final Map<String, EventStreamer> streamers = new HashMap<>();

    public BridgeConnection(Socket socket) {
        this.socket = socket;
    }

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------

    @Override
    public void run() {
        try (socket;
             var br = new BufferedReader(new InputStreamReader(socket.getInputStream()));
             var pw = new PrintWriter(new OutputStreamWriter(socket.getOutputStream()), true)) {

            synchronized (this) { out = pw; }

            String line;
            while ((line = br.readLine()) != null) {
                String trimmed = line.trim();
                if (!trimmed.isEmpty()) handleRequest(trimmed);
            }
        } catch (IOException e) {
            System.err.println("[ghidra-bridge] connection closed: " + e.getMessage());
        } finally {
            synchronized (this) { out = null; }
            stopAllStreamers();
            session.disconnect();
            System.err.println("[ghidra-bridge] connection handler exiting");
        }
    }

    // -------------------------------------------------------------------------
    // Thread-safe output
    // -------------------------------------------------------------------------

    private synchronized void writeLine(String json) {
        if (out != null) {
            out.println(json);
        }
    }

    // -------------------------------------------------------------------------
    // Dispatch
    // -------------------------------------------------------------------------

    private void handleRequest(String line) {
        JsonObject req;
        int id = -1;
        try {
            req = gson.fromJson(line, JsonObject.class);
            id  = req.has("id") ? req.get("id").getAsInt() : -1;
            String op = req.get("op").getAsString();

            switch (op) {
                case "ping":               respondOk(id); break;
                case "connect":            opConnect(id, req); break;
                case "disconnect":         opDisconnect(id); break;
                case "status":             opStatus(id); break;
                case "list_repos":         opListRepos(id); break;
                case "open_repo":          opOpenRepo(id, req); break;
                case "close_repo":         opCloseRepo(id, req); break;
                case "list_items":         opListItems(id, req); break;
                case "get_subfolders":     opGetSubfolders(id, req); break;
                case "checkout":           opCheckout(id, req); break;
                case "terminate_checkout": opTerminateCheckout(id, req); break;
                case "get_versions":       opGetVersions(id, req); break;
                case "get_checkouts":      opGetCheckouts(id, req); break;
                case "open_db":            opOpenDb(id, req); break;
                case "checkin":            opCheckin(id, req); break;
                case "download_binary":    opDownloadBinary(id, req); break;
                default:                   respondError(id, "unknown op: " + op); break;
            }
        } catch (Exception e) {
            String msg = e.getMessage();
            respondError(id, msg != null ? msg : e.getClass().getName());
        }
    }

    // -------------------------------------------------------------------------
    // Operation handlers
    // -------------------------------------------------------------------------

    private void opConnect(int id, JsonObject req) throws Exception {
        String host     = req.get("host").getAsString();
        int    port     = req.get("port").getAsInt();
        String user     = req.get("user").getAsString();
        String password = req.has("password") ? req.get("password").getAsString() : "";
        session.connect(host, port, user, password);
        JsonObject data = new JsonObject();
        data.addProperty("user", session.getConnectedUser());
        respondOk(id, data);
    }

    private void opDisconnect(int id) {
        stopAllStreamers();
        session.disconnect();
        respondOk(id);
    }

    private void opStatus(int id) {
        JsonObject data = new JsonObject();
        data.addProperty("connected", session.isConnected());
        data.addProperty("user", session.getConnectedUser());
        JsonArray repos = new JsonArray();
        session.getOpenRepos().forEach(repos::add);
        data.add("open_repos", repos);
        respondOk(id, data);
    }

    private void opListRepos(int id) throws Exception {
        String[] repos = session.listRepos();
        JsonObject data = new JsonObject();
        JsonArray arr = new JsonArray();
        for (String r : repos) arr.add(r);
        data.add("repos", arr);
        respondOk(id, data);
    }

    private void opOpenRepo(int id, JsonObject req) throws Exception {
        String name = req.get("repo").getAsString();
        session.openRepo(name);
        startStreamer(name);
        respondOk(id);
    }

    private void opCloseRepo(int id, JsonObject req) throws Exception {
        String name = req.get("repo").getAsString();
        stopStreamer(name);
        session.closeRepo(name);
        respondOk(id);
    }

    private void opListItems(int id, JsonObject req) throws Exception {
        String repo   = req.get("repo").getAsString();
        String folder = strOrDefault(req, "folder", "/");
        RepositoryItem[] items = session.listItems(repo, folder);
        JsonArray arr = new JsonArray();
        if (items != null) {
            for (RepositoryItem item : items) arr.add(itemToJson(item));
        }
        JsonObject data = new JsonObject();
        data.add("items", arr);
        data.addProperty("count", arr.size());
        System.err.println("[ghidra-bridge] list_items repo=" + repo + " folder=" + folder + " count=" + arr.size());
        respondOk(id, data);
    }

    private void opGetSubfolders(int id, JsonObject req) throws Exception {
        String repo   = req.get("repo").getAsString();
        String folder = strOrDefault(req, "folder", "/");
        String[] subs = session.getSubfolders(repo, folder);
        JsonArray arr = new JsonArray();
        if (subs != null) {
            for (String s : subs) arr.add(s);
        }
        JsonObject data = new JsonObject();
        data.add("subfolders", arr);
        data.addProperty("count", arr.size());
        System.err.println("[ghidra-bridge] get_subfolders repo=" + repo + " folder=" + folder + " count=" + arr.size());
        respondOk(id, data);
    }

    private void opCheckout(int id, JsonObject req) throws Exception {
        String repo        = req.get("repo").getAsString();
        String folder      = strOrDefault(req, "folder", "/");
        String item        = req.get("item").getAsString();
        String type        = strOrDefault(req, "type", "NORMAL");
        String projectPath = strOrDefault(req, "project_path",
                ItemCheckoutStatus.getProjectPath("binja-ghidra", false));
        ItemCheckoutStatus co = session.checkout(repo, folder, item, type, projectPath);
        JsonObject data = new JsonObject();
        data.add("checkout", checkoutToJson(co));
        respondOk(id, data);
    }

    private void opTerminateCheckout(int id, JsonObject req) throws Exception {
        String repo     = req.get("repo").getAsString();
        String folder   = strOrDefault(req, "folder", "/");
        String item     = req.get("item").getAsString();
        long   coId     = req.get("checkout_id").getAsLong();
        session.terminateCheckout(repo, folder, item, coId);
        respondOk(id);
    }

    private void opGetVersions(int id, JsonObject req) throws Exception {
        String repo   = req.get("repo").getAsString();
        String folder = strOrDefault(req, "folder", "/");
        String item   = req.get("item").getAsString();
        Version[] versions = session.getVersions(repo, folder, item);
        JsonArray arr = new JsonArray();
        for (Version v : versions) {
            JsonObject obj = new JsonObject();
            obj.addProperty("version", v.getVersion());
            obj.addProperty("time",    v.getCreateTime());
            obj.addProperty("user",    v.getUser());
            obj.addProperty("comment", v.getComment());
            arr.add(obj);
        }
        JsonObject data = new JsonObject();
        data.add("versions", arr);
        respondOk(id, data);
    }

    private void opGetCheckouts(int id, JsonObject req) throws Exception {
        String repo   = req.get("repo").getAsString();
        String folder = strOrDefault(req, "folder", "/");
        String item   = req.get("item").getAsString();
        ItemCheckoutStatus[] checkouts = session.getCheckouts(repo, folder, item);
        JsonArray arr = new JsonArray();
        for (ItemCheckoutStatus co : checkouts) arr.add(checkoutToJson(co));
        JsonObject data = new JsonObject();
        data.add("checkouts", arr);
        respondOk(id, data);
    }

    private void opOpenDb(int id, JsonObject req) throws Exception {
        String repo    = req.get("repo").getAsString();
        String folder  = strOrDefault(req, "folder", "/");
        String item    = req.get("item").getAsString();
        int    version = req.has("version") ? req.get("version").getAsInt() : -1;

        // Open the database buffer file read-only from the server.
        // minChangeDataVer = -1 means we only want the latest change data.
        ghidra.framework.remote.RepositoryHandle repoHandle = session.getRepo(repo);
        db.buffers.ManagedBufferFileHandle bufHandle =
            repoHandle.openDatabase(folder, item, version, -1);

        // Export tables → JSON (runs synchronously; blocks until done).
        com.google.gson.JsonObject dbData = DatabaseExporter.export(bufHandle);

        JsonObject data = new JsonObject();
        if (dbData.has("image_base")) data.add("image_base", dbData.get("image_base"));
        data.add("symbols",    dbData.get("symbols"));
        data.add("comments",   dbData.get("comments"));
        data.add("func_flags", dbData.get("func_flags"));
        respondOk(id, data);
    }

    private void opCheckin(int id, JsonObject req) throws Exception {
        String repo    = req.get("repo").getAsString();
        String folder  = strOrDefault(req, "folder", "/");
        String item    = req.get("item").getAsString();
        String comment = strOrDefault(req, "comment", "BN sync");
        JsonArray symbols  = req.has("symbols")  ? req.get("symbols").getAsJsonArray()  : new JsonArray();
        JsonArray comments = req.has("comments") ? req.get("comments").getAsJsonArray() : new JsonArray();

        ghidra.framework.remote.RepositoryHandle repoHandle = session.getRepo(repo);

        // Reuse an existing checkout by the current user if one is already active
        // (e.g. from a previous failed terminate), otherwise acquire a new one.
        long coId = -1;
        String currentUser = session.getConnectedUser();
        ghidra.framework.store.ItemCheckoutStatus[] existing =
                repoHandle.getCheckouts(folder, item);
        if (existing != null) {
            for (ghidra.framework.store.ItemCheckoutStatus s : existing) {
                if (currentUser != null && currentUser.equals(s.getUser())) {
                    coId = s.getCheckoutId();
                    System.err.println("[ghidra-bridge] reusing checkout_id=" + coId);
                    break;
                }
            }
        }
        boolean ownedCheckout = false;
        if (coId == -1) {
            String projectPath = ghidra.framework.store.ItemCheckoutStatus
                    .getProjectPath("binja-ghidra", false);
            ghidra.framework.store.ItemCheckoutStatus co =
                    repoHandle.checkout(folder, item,
                            ghidra.framework.store.CheckoutType.EXCLUSIVE, projectPath);
            if (co == null) {
                throw new java.io.IOException(
                    "Checkout failed — the item may already be exclusively checked out by another user");
            }
            coId = co.getCheckoutId();
            ownedCheckout = true;
            System.err.println("[ghidra-bridge] new checkout_id=" + coId);
        }
        System.err.println("[ghidra-bridge] checkin checkout_id=" + coId
                + " symbols=" + symbols.size() + " comments=" + comments.size());

        try {
            // 3-arg openDatabase opens in write mode for the given checkout.
            db.buffers.ManagedBufferFileHandle handle =
                    repoHandle.openDatabase(folder, item, coId);
            DatabaseImporter.apply(handle, symbols, comments, comment);
        } catch (Exception e) {
            if (ownedCheckout) terminateQuietly(repoHandle, folder, item, coId);
            throw e;
        }

        // Release only checkouts we created; reused checkouts belong to the caller.
        if (ownedCheckout) {
            // After saveCompleted(true) the server has created a new version.
            // Update the checkout pointer before terminating, as Ghidra's own
            // client does, so the server can correctly release the lock.
            try {
                ghidra.framework.store.Version[] versions = repoHandle.getVersions(folder, item);
                if (versions != null && versions.length > 0) {
                    int newVersion = versions[versions.length - 1].getVersion();
                    repoHandle.updateCheckoutVersion(folder, item, coId, newVersion);
                    System.err.println("[ghidra-bridge] updateCheckoutVersion → v" + newVersion);
                }
            } catch (Exception e) {
                System.err.println("[ghidra-bridge] updateCheckoutVersion failed: " + e.getMessage());
            }
            terminateQuietly(repoHandle, folder, item, coId);
        }

        System.err.println("[ghidra-bridge] checkin complete");
        JsonObject data = new JsonObject();
        data.addProperty("symbols_written",  symbols.size());
        data.addProperty("comments_written", comments.size());
        respondOk(id, data);
    }

    private void opDownloadBinary(int id, JsonObject req) throws Exception {
        String repo    = req.get("repo").getAsString();
        String folder  = strOrDefault(req, "folder", "/");
        String item    = req.get("item").getAsString();
        int    version = req.has("version") ? req.get("version").getAsInt() : -1;

        ghidra.framework.remote.RepositoryHandle repoHandle = session.getRepo(repo);
        db.buffers.ManagedBufferFileHandle bufHandle =
            repoHandle.openDatabase(folder, item, version, -1);

        db.buffers.ManagedBufferFileAdapter adapter =
            new db.buffers.ManagedBufferFileAdapter(bufHandle);
        db.DBHandle dbHandle = new db.DBHandle(adapter);
        try {
            List<ghidra.program.database.mem.BinaryExtractor.ExtractedFile> files =
                ghidra.program.database.mem.BinaryExtractor.extract(dbHandle);

            JsonArray arr = new JsonArray();
            for (ghidra.program.database.mem.BinaryExtractor.ExtractedFile ef : files) {
                JsonObject obj = new JsonObject();
                obj.addProperty("filename", ef.filename);
                obj.addProperty("size",     ef.bytes.length);
                obj.addProperty("data",     Base64.getEncoder().encodeToString(ef.bytes));
                arr.add(obj);
            }

            JsonObject data = new JsonObject();
            data.add("files", arr);
            respondOk(id, data);
        } finally {
            dbHandle.close();
        }
    }

    // -------------------------------------------------------------------------
    // Checkout helpers
    // -------------------------------------------------------------------------

    private static void terminateQuietly(
            ghidra.framework.remote.RepositoryHandle repo,
            String folder, String item, long coId) {
        try {
            repo.terminateCheckout(folder, item, coId, true);
            System.err.println("[ghidra-bridge] checkout terminated OK coId=" + coId);
        } catch (Exception e) {
            System.err.println("[ghidra-bridge] terminateCheckout failed: " + e.getMessage());
        }
    }

    // -------------------------------------------------------------------------
    // Event streaming
    // -------------------------------------------------------------------------

    private void startStreamer(String repoName) throws Exception {
        if (streamers.containsKey(repoName)) return;
        var repo     = session.getRepo(repoName);
        var streamer = new EventStreamer(repoName, repo, this::pushEvent);
        streamers.put(repoName, streamer);
        streamer.start();
    }

    private void stopStreamer(String repoName) {
        EventStreamer s = streamers.remove(repoName);
        if (s != null) s.stop();
    }

    private void stopAllStreamers() {
        streamers.values().forEach(EventStreamer::stop);
        streamers.clear();
    }

    private void pushEvent(String repoName, RepositoryChangeEvent evt) {
        JsonObject msg = new JsonObject();
        msg.addProperty("event", "repo_changed");
        msg.addProperty("repo",  repoName);
        msg.addProperty("type",  eventTypeName(evt.type));
        if (evt.parentPath    != null) msg.addProperty("parent_path",     evt.parentPath);
        if (evt.name          != null) msg.addProperty("name",            evt.name);
        if (evt.newParentPath != null) msg.addProperty("new_parent_path", evt.newParentPath);
        if (evt.newName       != null) msg.addProperty("new_name",        evt.newName);
        writeLine(gson.toJson(msg));
    }

    // -------------------------------------------------------------------------
    // Response helpers
    // -------------------------------------------------------------------------

    private void respondOk(int id) {
        respondOk(id, null);
    }

    private void respondOk(int id, JsonObject extra) {
        JsonObject resp = new JsonObject();
        resp.addProperty("id", id);
        resp.addProperty("ok", true);
        if (extra != null) {
            for (Map.Entry<String, JsonElement> e : extra.entrySet()) {
                resp.add(e.getKey(), e.getValue());
            }
        }
        writeLine(gson.toJson(resp));
    }

    private void respondError(int id, String error) {
        JsonObject resp = new JsonObject();
        resp.addProperty("id",    id);
        resp.addProperty("ok",    false);
        resp.addProperty("error", error);
        writeLine(gson.toJson(resp));
    }

    // -------------------------------------------------------------------------
    // Serialisation helpers
    // -------------------------------------------------------------------------

    private static JsonObject itemToJson(RepositoryItem item) {
        JsonObject o = new JsonObject();
        o.addProperty("name",         item.getName());
        o.addProperty("parent_path",  item.getParentPath());
        o.addProperty("path",         item.getPathName());
        o.addProperty("file_id",      item.getFileID());
        o.addProperty("item_type",    item.getItemType());
        o.addProperty("content_type", item.getContentType());
        o.addProperty("version",      item.getVersion());
        o.addProperty("version_time", item.getVersionTime());
        if (item.getTextData() != null) o.addProperty("text_data", item.getTextData());
        return o;
    }

    private static JsonObject checkoutToJson(ItemCheckoutStatus co) {
        JsonObject o = new JsonObject();
        o.addProperty("id",           co.getCheckoutId());
        o.addProperty("type",         co.getCheckoutType().name());
        o.addProperty("user",         co.getUser());
        o.addProperty("version",      co.getCheckoutVersion());
        o.addProperty("time",         co.getCheckoutTime());
        o.addProperty("project_path", co.getProjectPath());
        return o;
    }

    private static String eventTypeName(int type) {
        if (type == RepositoryChangeEvent.REP_FOLDER_CREATED)    return "REP_FOLDER_CREATED";
        if (type == RepositoryChangeEvent.REP_ITEM_CREATED)      return "REP_ITEM_CREATED";
        if (type == RepositoryChangeEvent.REP_FOLDER_DELETED)    return "REP_FOLDER_DELETED";
        if (type == RepositoryChangeEvent.REP_FOLDER_MOVED)      return "REP_FOLDER_MOVED";
        if (type == RepositoryChangeEvent.REP_FOLDER_RENAMED)    return "REP_FOLDER_RENAMED";
        if (type == RepositoryChangeEvent.REP_ITEM_DELETED)      return "REP_ITEM_DELETED";
        if (type == RepositoryChangeEvent.REP_ITEM_RENAMED)      return "REP_ITEM_RENAMED";
        if (type == RepositoryChangeEvent.REP_ITEM_MOVED)        return "REP_ITEM_MOVED";
        if (type == RepositoryChangeEvent.REP_ITEM_CHANGED)      return "REP_ITEM_CHANGED";
        if (type == RepositoryChangeEvent.REP_OPEN_HANDLE_COUNT) return "REP_OPEN_HANDLE_COUNT";
        return "UNKNOWN_" + type;
    }

    private static String strOrDefault(JsonObject obj, String key, String def) {
        return obj.has(key) ? obj.get(key).getAsString() : def;
    }
}

package com.ghidra_svr.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;

import db.DBHandle;
import db.DBRecord;
import db.Table;
import db.buffers.ManagedBufferFileAdapter;
import db.buffers.ManagedBufferFileHandle;
import ghidra.util.exception.CancelledException;
import ghidra.util.task.TaskMonitor;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

/**
 * Applies symbol renames and comment updates from Binary Ninja to a
 * Ghidra program database, then checks in the new version.
 *
 * The caller must hold an exclusive checkout for the item before calling
 * apply(); the checkout is implicitly committed by db.save() and should
 * be terminated by the caller afterwards.
 */
public class DatabaseImporter {

    private static final String SYMBOLS_TABLE  = "Symbols";
    private static final int SYM_NAME_COL      = 0;
    private static final int SYM_FLAGS_COL     = 4;
    private static final int SOURCE_SHIFT      = 6;

    private static final String COMMENTS_TABLE = "Comments";
    private static final int COMM_EOL_COL      = 0;
    private static final int COMM_PRE_COL      = 1;
    private static final int COMM_POST_COL     = 2;
    private static final int COMM_PLATE_COL    = 3;
    private static final int COMM_REP_COL      = 4;

    /**
     * Apply changes and create a new version on the server.
     *
     * @param handle         writable handle obtained from openDatabase(folder,item,checkoutId)
     * @param symbols        JSON array of {key, name} — renamed symbols only
     * @param comments       JSON array of {key, eol?, pre?, post?, plate?, rep?}
     * @param versionComment comment for the new version
     */
    public static void apply(ManagedBufferFileHandle handle,
                              JsonArray symbols,
                              JsonArray comments,
                              String versionComment) throws IOException {
        ManagedBufferFileAdapter adapter = new ManagedBufferFileAdapter(handle);
        DBHandle db = new DBHandle(adapter);

        long txId = db.startTransaction();
        boolean committed = false;
        try {
            if (symbols  != null && symbols.size()  > 0) applySymbols(db, symbols);
            if (comments != null && comments.size() > 0) applyComments(db, comments);
            db.endTransaction(txId, true);
            committed = true;
        } finally {
            if (!committed) db.endTransaction(txId, false);
        }

        // save() writes modified buffers to the managed save-file and calls
        // saveCompleted(true), which creates a new version on the server.
        handle.setVersionComment(versionComment != null ? versionComment : "");
        try {
            db.save(versionComment, null, TaskMonitor.DUMMY);
        } catch (CancelledException e) {
            throw new IOException("save cancelled unexpectedly", e);
        } finally {
            db.close();
        }
    }

    // -------------------------------------------------------------------------

    private static void applySymbols(DBHandle db, JsonArray symbols) throws IOException {
        Table table = db.getTable(SYMBOLS_TABLE);
        if (table == null) {
            System.err.println("[ghidra-bridge] WARNING: Symbols table not found");
            return;
        }
        int count = 0;
        for (JsonElement el : symbols) {
            JsonObject s = el.getAsJsonObject();
            long   key  = s.get("key").getAsLong();
            String name = s.get("name").getAsString();
            if (name == null || name.isEmpty()) continue;

            DBRecord rec = table.getRecord(key);
            if (rec == null) continue;

            rec.setString(SYM_NAME_COL, name);
            // Promote source to USER_DEFINED (3 << 6), preserve lower flag bits.
            byte flags = rec.getByteValue(SYM_FLAGS_COL);
            flags = (byte) ((flags & 0x3F) | (3 << SOURCE_SHIFT));
            rec.setByteValue(SYM_FLAGS_COL, flags);
            table.putRecord(rec);
            ++count;
        }
        System.err.println("[ghidra-bridge] symbols written: " + count);
    }

    private static void applyComments(DBHandle db, JsonArray comments) throws IOException {
        Table table = db.getTable(COMMENTS_TABLE);
        if (table == null) {
            System.err.println("[ghidra-bridge] WARNING: Comments table not found");
            return;
        }

        // Build VA→encodedKey map from the address table so we can encode
        // comments added in BN at addresses that had no Ghidra comment.
        Map<Long, Long> addrMap = buildAddrMap(db);

        int count = 0;
        for (JsonElement el : comments) {
            JsonObject c = el.getAsJsonObject();

            // Resolve the DB record key: prefer the pre-computed encoded key,
            // fall back to encoding the Ghidra VA via the address map.
            long key;
            String keyStr = c.has("key") ? c.get("key").getAsString() : "";
            if (!keyStr.isEmpty()) {
                key = parseHexLong(keyStr);
            } else if (c.has("va")) {
                long va = parseHexLong(c.get("va").getAsString());
                key = encodeVA(va, addrMap);
                if (key < 0) {
                    System.err.println("[ghidra-bridge] could not encode VA 0x"
                        + Long.toHexString(va) + " — skipping");
                    continue;
                }
            } else {
                continue;
            }

            DBRecord rec = table.getRecord(key);
            if (rec == null) rec = table.getSchema().createRecord(key);

            setOrClear(rec, COMM_EOL_COL,   c, "eol");
            setOrClear(rec, COMM_PRE_COL,   c, "pre");
            setOrClear(rec, COMM_POST_COL,  c, "post");
            setOrClear(rec, COMM_PLATE_COL, c, "plate");
            setOrClear(rec, COMM_REP_COL,   c, "rep");
            table.putRecord(rec);
            ++count;
        }
        System.err.println("[ghidra-bridge] comments written: " + count);
    }

    /** Build ADDRESS MAP: rowKey → base VA (mirrors DatabaseExporter.buildAddressMap). */
    private static Map<Long, Long> buildAddrMap(DBHandle db) {
        Map<Long, Long> map = new HashMap<>();
        Table t = db.getTable("ADDRESS MAP");
        if (t == null) return map;
        try {
            db.RecordIterator iter = t.iterator();
            while (iter.hasNext()) {
                DBRecord rec = iter.next();
                long base = 0;
                try { base = rec.getLongValue(1); } catch (Exception e) {
                    try { base = rec.getIntValue(1) & 0xFFFFFFFFL; } catch (Exception ignored) {}
                }
                map.put(rec.getKey(), base);
            }
        } catch (Exception e) {
            System.err.println("[ghidra-bridge] addrMap load error: " + e.getMessage());
        }
        return map;
    }

    /**
     * Encode a Ghidra VA to a DB record key using the address map.
     * Tries direct (new-format) lookup first, then old-format (RAM_SPACE_PREFIX).
     * Returns -1 if the VA cannot be encoded.
     */
    private static long encodeVA(long va, Map<Long, Long> addrMap) {
        long bestKey = -1, bestBase = -1;
        // Find the segment with the largest base ≤ va.
        for (Map.Entry<Long, Long> e : addrMap.entrySet()) {
            long base = e.getValue();
            if (base <= va && base > bestBase) {
                bestBase = base;
                bestKey  = e.getKey();
            }
        }
        if (bestKey < 0) return -1;

        long offset = va - bestBase;
        // New format: row key IS the full segKey stored in encoded addresses.
        // Old format: segKey = 0x20000000L | rowKey (RAM_SPACE_PREFIX << 8 | rowKey).
        // We detect old format by checking if bestKey is a small sequential index (0–255).
        long segKey = (bestKey < 256) ? (0x20000000L | bestKey) : bestKey;
        return (segKey << 32) | offset;
    }

    private static void setOrClear(DBRecord rec, int col, JsonObject obj, String field) {
        String val = obj.has(field) ? obj.get(field).getAsString() : null;
        rec.setString(col, (val != null && !val.isEmpty()) ? val : null);
    }

    private static long parseHexLong(String s) {
        if (s == null || s.isEmpty()) return 0;
        String t = (s.startsWith("0x") || s.startsWith("0X")) ? s.substring(2) : s;
        return Long.parseUnsignedLong(t, 16);
    }
}

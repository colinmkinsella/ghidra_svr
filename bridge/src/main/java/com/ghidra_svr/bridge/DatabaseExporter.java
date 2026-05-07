package com.ghidra_svr.bridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import db.DBHandle;
import db.DBRecord;
import db.RecordIterator;
import db.Table;
import db.buffers.ManagedBufferFileAdapter;
import db.buffers.ManagedBufferFileHandle;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

/**
 * Reads a Ghidra program database (via an RMI buffer-file handle) and
 * extracts symbols, comments, and function flags for Binary Ninja.
 *
 * Address encoding (old "ADDRESS MAP" format)
 * -------------------------------------------
 * Encoded address = ((spacePrefix | rowKey) << 32) | offset
 *
 * The ADDRESS MAP table has row keys 0, 1, 2, ... (segment indices).
 * Each row's col-1 stores the base VA for that segment.
 * spacePrefix identifies the address space type:
 *   0x20000000 = main RAM space (code + data)
 *   0x50000000 = external import space  (not a real VA, skip)
 *   0x60000000 = stack / local-var space (not a real VA, skip)
 *
 * Decoding: rowKey = segKey & 0xFF  (only valid when upper 24 bits = 0x200000)
 *           VA     = addrMap[rowKey] + offset
 */
public class DatabaseExporter {

    // ---- Address Map table ---------------------------------------------------
    private static final String ADDR_MAP_TABLE = "ADDRESS MAP";
    // Upper 24 bits of the segment key for the main RAM address space.
    private static final long RAM_SPACE_PREFIX = 0x200000L;

    // ---- Symbols table -------------------------------------------------------
    private static final String SYMBOLS_TABLE   = "Symbols";
    private static final int SYM_NAME_COL       = 0;  // StringField
    private static final int SYM_ADDR_COL       = 1;  // LongField (encoded)
    private static final int SYM_NAMESPACE_COL  = 2;  // LongField
    private static final int SYM_TYPE_COL       = 3;  // ByteField
    private static final int SYM_FLAGS_COL      = 4;  // ByteField

    private static final byte SYM_TYPE_LABEL       = 0;
    // Ghidra < 11 uses ordinal 4 for FUNCTION; Ghidra 11+ uses ordinal 5.
    private static final byte SYM_TYPE_FUNCTION_OLD = 4;
    private static final byte SYM_TYPE_FUNCTION_NEW = 5;
    private static final byte SYM_TYPE_EXTERNAL     = 8;

    private static final int SOURCE_SHIFT = 6;
    private static final int SOURCE_MASK  = 0x03;

    // ---- Comments table ------------------------------------------------------
    private static final String COMMENTS_TABLE = "Comments";
    private static final int COMM_EOL_COL   = 0;
    private static final int COMM_PRE_COL   = 1;
    private static final int COMM_POST_COL  = 2;
    private static final int COMM_PLATE_COL = 3;
    private static final int COMM_REP_COL   = 4;

    // ---- Functions table -----------------------------------------------------
    private static final String FUNCTIONS_TABLE = "Function Data";
    private static final int FUNC_FLAGS_COL     = 4;
    private static final int FUNC_FLAG_THUNK    = 0x01;
    private static final int FUNC_FLAG_NRET     = 0x02;
    private static final int FUNC_FLAG_INLINE   = 0x04;

    // =========================================================================

    public static JsonObject export(ManagedBufferFileHandle remoteHandle) throws IOException {
        ManagedBufferFileAdapter adapter = new ManagedBufferFileAdapter(remoteHandle);
        DBHandle db = new DBHandle(adapter);
        try {
            Map<Long, Long> addrMap = buildAddressMap(db);
            System.err.println("[ghidra-bridge] address map loaded: " + addrMap.size() + " segments");

            long imageBase = addrMap.getOrDefault(0L, 0L);
            System.err.println("[ghidra-bridge] image_base=0x" + Long.toHexString(imageBase));

            JsonObject out = new JsonObject();
            out.addProperty("image_base", addrHex(imageBase));
            out.add("symbols",    exportSymbols(db, addrMap));
            out.add("comments",   exportComments(db, addrMap));
            out.add("func_flags", exportFuncFlags(db));
            return out;
        } finally {
            db.close();
        }
    }

    // -------------------------------------------------------------------------
    // Address map
    // -------------------------------------------------------------------------

    private static Map<Long, Long> buildAddressMap(DBHandle db) {
        Map<Long, Long> map = new HashMap<>();
        Table table = db.getTable(ADDR_MAP_TABLE);
        if (table == null) {
            System.err.println("[ghidra-bridge] ADDRESS MAP table not found");
            return map;
        }
        try {
            RecordIterator iter = table.iterator();
            while (iter.hasNext()) {
                DBRecord rec = iter.next();
                long rowKey = rec.getKey();
                long baseVA = readBaseVA(rec);
                map.put(rowKey, baseVA);
                System.err.println("[ghidra-bridge] addrmap row=0x"
                    + Long.toHexString(rowKey) + " base=0x" + Long.toHexString(baseVA));
            }
        } catch (Exception e) {
            System.err.println("[ghidra-bridge] error reading ADDRESS MAP: "
                + e.getClass().getName() + ": " + e.getMessage());
        }
        return map;
    }

    /** Try every possible accessor for the base VA stored in ADDRESS MAP col 1 (then col 0). */
    private static long readBaseVA(DBRecord rec) {
        try { return rec.getLongValue(1); } catch (Exception ignored) {}
        try { return rec.getIntValue(1) & 0xFFFFFFFFL; } catch (Exception ignored) {}
        try { return rec.getLongValue(0); } catch (Exception ignored) {}
        try { return rec.getIntValue(0) & 0xFFFFFFFFL; } catch (Exception ignored) {}
        for (int col : new int[]{1, 0}) {
            try {
                byte[] b = rec.getBinaryData(col);
                if (b != null && b.length >= 4) {
                    long v = 0;
                    for (int i = 0; i < Math.min(b.length, 8); i++)
                        v = (v << 8) | (b[i] & 0xFF);
                    return v;
                }
            } catch (Exception ignored) {}
        }
        return 0;
    }

    /**
     * Decode a Ghidra-encoded address to a real virtual address.
     *
     * Returns -1 for addresses in the external import space (0x50000000),
     * local-var space (0x60000000), or any other non-RAM space.
     * Callers must skip symbols/comments where decode returns -1.
     */
    private static long decode(long encoded, Map<Long, Long> addrMap) {
        long segKey = encoded >> 32;
        long offset = encoded & 0xFFFFFFFFL;

        // Direct lookup — works when the ADDRESS MAP row key IS the full segKey.
        if (!addrMap.isEmpty()) {
            Long base = addrMap.get(segKey);
            if (base != null) return base + offset;

            // Old format: row key = lower byte of segKey, valid only for RAM space
            // (upper 24 bits of segKey == RAM_SPACE_PREFIX = 0x200000).
            if ((segKey >> 8) == RAM_SPACE_PREFIX) {
                base = addrMap.get(segKey & 0xFFL);
                if (base != null) return base + offset;
            }

            // Non-RAM space (external imports, stack, etc.) — no valid VA.
            return -1L;
        }

        // No address map available — return raw offset as best effort.
        return offset;
    }

    // -------------------------------------------------------------------------
    // Table exporters
    // -------------------------------------------------------------------------

    private static JsonArray exportSymbols(DBHandle db, Map<Long, Long> addrMap) throws IOException {
        JsonArray arr = new JsonArray();
        Table table = db.getTable(SYMBOLS_TABLE);
        if (table == null) return arr;

        RecordIterator iter = table.iterator();
        while (iter.hasNext()) {
            DBRecord rec = iter.next();

            byte type = rec.getByteValue(SYM_TYPE_COL);
            if (!isExportedType(type)) continue;

            String name = rec.getString(SYM_NAME_COL);
            if (name == null || name.isEmpty()) continue;

            long va = decode(rec.getLongValue(SYM_ADDR_COL), addrMap);
            if (va < 0) continue; // external or non-RAM space

            byte flags  = rec.getByteValue(SYM_FLAGS_COL);
            int  source = (flags >> SOURCE_SHIFT) & SOURCE_MASK;

            JsonObject sym = new JsonObject();
            sym.addProperty("key",    rec.getKey());
            sym.addProperty("name",   name);
            sym.addProperty("addr",   addrHex(va));
            // Normalize: C++ expects 4=FUNCTION regardless of Ghidra version ordinal.
            sym.addProperty("type",   normalizeType(type));
            sym.addProperty("source", source);
            sym.addProperty("ns",     rec.getLongValue(SYM_NAMESPACE_COL));
            arr.add(sym);
        }
        return arr;
    }

    private static JsonArray exportComments(DBHandle db, Map<Long, Long> addrMap) throws IOException {
        JsonArray arr = new JsonArray();
        Table table = db.getTable(COMMENTS_TABLE);
        if (table == null) return arr;

        RecordIterator iter = table.iterator();
        while (iter.hasNext()) {
            DBRecord rec = iter.next();

            String eol   = emptyIfNull(rec.getString(COMM_EOL_COL));
            String pre   = emptyIfNull(rec.getString(COMM_PRE_COL));
            String post  = emptyIfNull(rec.getString(COMM_POST_COL));
            String plate = emptyIfNull(rec.getString(COMM_PLATE_COL));
            String rep   = emptyIfNull(rec.getString(COMM_REP_COL));

            if (eol.isEmpty() && pre.isEmpty() && post.isEmpty()
                    && plate.isEmpty() && rep.isEmpty()) continue;

            long va = decode(rec.getKey(), addrMap);
            if (va < 0) continue; // external or non-RAM space

            JsonObject comm = new JsonObject();
            comm.addProperty("addr", addrHex(va));
            comm.addProperty("key",  addrHex(rec.getKey())); // encoded addr for write-back
            if (!eol.isEmpty())   comm.addProperty("eol",   eol);
            if (!pre.isEmpty())   comm.addProperty("pre",   pre);
            if (!post.isEmpty())  comm.addProperty("post",  post);
            if (!plate.isEmpty()) comm.addProperty("plate", plate);
            if (!rep.isEmpty())   comm.addProperty("rep",   rep);
            arr.add(comm);
        }
        return arr;
    }

    private static JsonArray exportFuncFlags(DBHandle db) throws IOException {
        JsonArray arr = new JsonArray();
        Table table = db.getTable(FUNCTIONS_TABLE);
        if (table == null) return arr;

        RecordIterator iter = table.iterator();
        while (iter.hasNext()) {
            DBRecord rec = iter.next();
            byte flags = rec.getByteValue(FUNC_FLAGS_COL);
            if (flags == 0) continue;

            JsonObject f = new JsonObject();
            f.addProperty("key",    rec.getKey());
            f.addProperty("thunk",  (flags & FUNC_FLAG_THUNK)  != 0);
            f.addProperty("no_ret", (flags & FUNC_FLAG_NRET)   != 0);
            f.addProperty("inline", (flags & FUNC_FLAG_INLINE) != 0);
            arr.add(f);
        }
        return arr;
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    private static boolean isExportedType(byte type) {
        return type == SYM_TYPE_LABEL
            || type == SYM_TYPE_FUNCTION_OLD
            || type == SYM_TYPE_FUNCTION_NEW
            || type == SYM_TYPE_EXTERNAL;
    }

    /**
     * Normalize the Ghidra symbol type for the C++ side.
     * C++ GhidraSymbolType enum expects: Label=0, Function=4, External=8.
     * Ghidra 11+ uses ordinal 5 for FUNCTION; map it back to 4.
     */
    private static int normalizeType(byte type) {
        if (type == SYM_TYPE_FUNCTION_NEW) return SYM_TYPE_FUNCTION_OLD;
        return type & 0xFF;
    }

    private static String addrHex(long addr) {
        return "0x" + Long.toUnsignedString(addr, 16);
    }

    private static String emptyIfNull(String s) {
        return s == null ? "" : s;
    }
}

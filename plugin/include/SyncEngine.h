#pragma once
#include "GhidraConnection.h"
#include <binaryninjaapi.h>
#include <ui/uitypes.h>
#include <unordered_map>

struct SyncResult {
    int      symbolsApplied  = 0;
    int      commentsApplied = 0;
    int      flagsApplied    = 0;
    uint64_t addrMin         = UINT64_MAX;
    uint64_t addrMax         = 0;
    std::string sampleSymbol;

    // Write-back maps — passed to GhidraConnection::storeCheckinState().
    std::unordered_map<uint64_t, int64_t>     addrToKey;             // bnAddr → symbol key
    std::unordered_map<uint64_t, std::string> addrToOriginalName;    // bnAddr → original name
    std::unordered_map<uint64_t, std::string> addrToCommentKey;      // bnAddr → encoded Ghidra addr
    std::unordered_map<uint64_t, std::string> addrToOriginalComment;     // bnAddr → imported address-level comment
    std::unordered_map<uint64_t, std::string> addrToOriginalFuncComment; // bnAddr → imported plate/function comment
    uint64_t imageBase = 0;  // Ghidra segment-0 VA, needed for encoding new comment addresses
};

/**
 * Applies a GhidraDbExport to a Binary Ninja BinaryView.
 *
 * All methods block; call from a BN background worker thread, never from the
 * UI thread.
 */
class SyncEngine {
public:
    static SyncResult applyToView(BinaryViewRef view,
                                  const GhidraDbExport& data);

private:
    static BinaryNinja::Ref<BinaryNinja::TagType>
        getOrCreateTagType(BinaryViewRef view, const std::string& name);
};

#include "SyncEngine.h"
#include <binaryninjaapi.h>
#include <unordered_map>

using namespace BinaryNinja;

// ---------------------------------------------------------------------------
// Tag-type helper — creates a new TagType if it doesn't exist yet
// ---------------------------------------------------------------------------

Ref<TagType> SyncEngine::getOrCreateTagType(Ref<BinaryView> view,
                                             const std::string& name) {
    auto tt = view->GetTagType(name);
    if (!tt) {
        tt = new TagType(view.GetPtr(), name, "G");
        view->AddTagType(tt);
    }
    return tt;
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

SyncResult SyncEngine::applyToView(Ref<BinaryView> view, const GhidraDbExport& data) {
    SyncResult result;
    if (!view) return result;

    result.imageBase = data.imageBase;

    Platform* platform = view->GetDefaultPlatform();

    // Compute address rebase: Ghidra may have analyzed the binary at a different
    // image base than BN.  The bridge exports the Ghidra base for the main
    // ("ram") segment (row 0 of the ADDRESS MAP).
    uint64_t bnBase     = view->GetStart();
    uint64_t ghidraBase = data.imageBase;
    int64_t  rebase     = (int64_t)bnBase - (int64_t)ghidraBase;
    BinaryNinja::LogInfo("ghidra-bridge: imageBase=0x%llx bnBase=0x%llx rebase=0x%llx",
                         (unsigned long long)ghidraBase, (unsigned long long)bnBase,
                         (unsigned long long)(uint64_t)rebase);

    auto applyRebase = [&](uint64_t addr) -> uint64_t {
        return (uint64_t)((int64_t)addr + rebase);
    };

    // Needed for funcFlags: symbol key → virtual address mapping.
    std::unordered_map<int64_t, uint64_t> keyToAddr;
    keyToAddr.reserve(data.symbols.size());

    // -----------------------------------------------------------------------
    // Symbols
    // -----------------------------------------------------------------------
    for (const auto& sym : data.symbols) {
        BNSymbolType bnType;
        if (sym.type == GhidraSymbolType::Function)
            bnType = FunctionSymbol;
        else if (sym.type == GhidraSymbolType::External)
            bnType = ImportedFunctionSymbol;
        else
            bnType = DataSymbol;

        uint64_t addr = applyRebase(sym.addr);
        view->DefineUserSymbol(new Symbol(bnType, sym.name, addr));
        ++result.symbolsApplied;

        if (addr < result.addrMin) result.addrMin = addr;
        if (addr > result.addrMax) result.addrMax = addr;
        if (result.sampleSymbol.empty() && sym.type == GhidraSymbolType::Function)
            result.sampleSymbol = sym.name;

        result.addrToKey[addr]          = sym.key;
        result.addrToOriginalName[addr] = sym.name;
        if (sym.type == GhidraSymbolType::Function) {
            keyToAddr[sym.key] = addr;
        }
    }

    // -----------------------------------------------------------------------
    // Comments
    // -----------------------------------------------------------------------
    for (const auto& comm : data.comments) {
        uint64_t caddr = applyRebase(comm.addr);
        if (!comm.encodedKey.empty())
            result.addrToCommentKey[caddr] = comm.encodedKey;
        std::string text;
        for (const std::string* part : {&comm.eol, &comm.rep, &comm.pre, &comm.post}) {
            if (part->empty()) continue;
            if (!text.empty()) text += '\n';
            text += *part;
        }
        if (!text.empty()) {
            view->SetCommentForAddress(caddr, text);
            result.addrToOriginalComment[caddr] = text;
            ++result.commentsApplied;
        }

        // Plate comment → BN function-level comment.
        if (!comm.plate.empty()) {
            auto funcs = view->GetAnalysisFunctionsForAddress(caddr);
            for (auto& func : funcs) {
                if (func->GetStart() == caddr) {
                    func->SetComment(comm.plate);
                    result.addrToOriginalFuncComment[caddr] = comm.plate;
                    break;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Function flags  (thunk / noReturn / inline → BN user tags)
    // -----------------------------------------------------------------------
    Ref<TagType> ttThunk, ttNoRet, ttInline;

    for (const auto& ff : data.funcFlags) {
        auto it = keyToAddr.find(ff.key);
        if (it == keyToAddr.end()) continue;
        if (!platform) continue;

        auto func = view->GetAnalysisFunction(platform, it->second);
        if (!func) continue;

        if (ff.thunk) {
            if (!ttThunk) ttThunk = getOrCreateTagType(view, "Ghidra: Thunk");
            func->CreateUserFunctionTag(ttThunk, "");
        }
        if (ff.noReturn) {
            if (!ttNoRet) ttNoRet = getOrCreateTagType(view, "Ghidra: NoReturn");
            func->CreateUserFunctionTag(ttNoRet, "");
        }
        if (ff.isInline) {
            if (!ttInline) ttInline = getOrCreateTagType(view, "Ghidra: Inline");
            func->CreateUserFunctionTag(ttInline, "");
        }
        ++result.flagsApplied;
    }

    view->UpdateAnalysis();
    return result;
}

#include <Windows.h>
#include <cstring>
#include "StabilityFix.h"
#include "../Config.h"
#include "../Logger.h"
#include "../Patch.h"

namespace BullyDE {
namespace {

// ---------------------------------------------------------------------------
// Heap free-list null dereferences
// ---------------------------------------------------------------------------
//
// The game keeps free memory blocks on a doubly-linked list, with an array of
// bucket heads indexed by block size. Two routines maintain that list, and
// both dereference a neighbour pointer without checking it first:
//
//   sub_5EEBF0  insert               sub_5EECA0  unlink
//     8b 51 10  mov edx,[ecx+10h]      8b 48 10  mov ecx,[eax+10h]  ; next
//     89 42 14  mov [edx+14h],eax  **  8b 50 14  mov edx,[eax+14h]  ; prev
//                                      89 51 14  mov [ecx+14h],edx  **
//                                      89 4a 10  mov [edx+10h],ecx  **
//
// When a neighbour is null those become writes to addresses 0x14 and 0x10.
// That is an access violation inside the allocator, which surfaces as a crash
// with no useful stack, because it happens underneath whatever asked for
// memory rather than in the code that caused it.
//
// The replacements below are a faithful reimplementation with null checks
// added. They also verify a neighbour points back at this node before
// unlinking through it, which costs nothing on a healthy list and contains the
// damage on a corrupted one.
//
// Block layout, from the offsets the two routines use:
//     +0x00  uint32  size
//     +0x10  Block*  next
//     +0x14  Block*  prev
//
// Calling convention: both are thiscall returning in eax, with the bucket
// array (and, for insert, the node) passed on the stack. __fastcall with an
// unused second parameter reproduces that exactly -- ecx holds the first
// argument, edx is ignored, the rest go on the stack, and the callee cleans
// them. That yields `retn 8` for insert and `retn 4` for unlink, matching the
// originals. Both return values are preserved; callers do use them, and at
// 0x005EF2B2 the insert result becomes the caller's own return value.

struct Block {
    uint32_t size;      // +0x00
    uint32_t pad[3];    // +0x04
    Block*   next;      // +0x10
    Block*   prev;      // +0x14
};
static_assert(offsetof(Block, next) == 0x10, "Block::next must sit at +0x10");
static_assert(offsetof(Block, prev) == 0x14, "Block::prev must sit at +0x14");

constexpr uintptr_t kFreeListInsert = 0x005EEBF0;
constexpr uintptr_t kFreeListUnlink = 0x005EECA0;

// Longer than the 5 bytes the jump needs, so a shifted build fails the check
// instead of being patched.
const uint8_t kInsertVanilla[] = { 0x8B, 0x51, 0x10, 0x8B, 0x44, 0x24, 0x04, 0x89 };
const uint8_t kUnlinkVanilla[] = { 0x8B, 0xC1, 0x8B, 0x48, 0x10, 0x8B, 0x50, 0x14 };

volatile LONG s_guardHits = 0;

inline void NoteGuard() { InterlockedIncrement(&s_guardHits); }

// Size-to-bucket mapping, transcribed from the loop both routines share:
//     idx = 0; if (size >= 0x20) do { if (idx >= 19) break; ++idx; }
//                               while (size >> (idx + 5));
// The cap of 19 is the game's own, and keeps the shift under 32.
int BucketIndex(uint32_t size) {
    int idx = 0;
    if (size >= 0x20) {
        do {
            if (idx >= 19) break;
            ++idx;
        } while ((size >> (idx + 5)) != 0);
    }
    return idx;
}

// Replaces sub_5EEBF0. Returns the inserted node, as the original does.
Block* __fastcall Hook_FreeListInsert(Block* self, void*, Block* node, Block** buckets) {
    if (node == nullptr || buckets == nullptr) {
        NoteGuard();
        return node;
    }

    if (self != nullptr) {
        Block* next = self->next;
        node->next = next;
        node->prev = self;
        if (next != nullptr) {
            next->prev = node;   // vanilla writes this unconditionally
        } else {
            NoteGuard();
        }
        self->next = node;
    } else {
        node->next = nullptr;
        node->prev = nullptr;
        NoteGuard();
    }

    const uint32_t size = node->size;
    const int idx = BucketIndex(size);
    Block* head = buckets[idx];
    if (head == nullptr || head->next == nullptr || head->size > size) {
        buckets[idx] = node;
    }
    return node;
}

// Replaces sub_5EECA0. Returns the node, or its successor when this node was
// the bucket head -- the original's two return paths, preserved.
Block* __fastcall Hook_FreeListUnlink(Block* self, void*, Block** buckets) {
    if (self == nullptr) {
        NoteGuard();
        return nullptr;
    }
    if (buckets == nullptr) {
        NoteGuard();
        return self;
    }

    Block* next = self->next;
    Block* prev = self->prev;

    if (next != nullptr) {
        if (next->prev == self) {
            next->prev = prev;   // vanilla writes this unconditionally
        } else {
            NoteGuard();
        }
    } else {
        NoteGuard();
    }

    if (prev != nullptr) {
        if (prev->next == self) {
            prev->next = next;   // vanilla writes this unconditionally
        } else {
            NoteGuard();
        }
    } else {
        NoteGuard();
    }

    Block* result = self;
    const int idx = BucketIndex(self->size);
    if (buckets[idx] == self) {
        result = next;
        if (next == nullptr) {
            buckets[idx] = nullptr;
            NoteGuard();
        } else {
            const int idx2 = BucketIndex(next->size);
            buckets[idx] = (idx != idx2) ? nullptr : next;
        }
    }
    return result;
}

// Verifies the vanilla prologue, then redirects the function with a jump.
bool RedirectFunction(const char* what, uintptr_t site,
                      const uint8_t* vanilla, size_t vanillaLen, void* target) {
    if (!Patch::Verify(what, site, vanilla, vanillaLen)) {
        return false;
    }

    uint8_t jump[5];
    jump[0] = 0xE9; // JMP rel32
    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(target) - (site + 5));
    std::memcpy(&jump[1], &rel, sizeof(rel));

    return Patch::Bytes(what, site, vanilla, jump, sizeof(jump));
}

} // namespace

long StabilityFix::GuardHits() {
    return InterlockedCompareExchange(&s_guardHits, 0, 0);
}

void StabilityFix::Report() {
    const long hits = GuardHits();
    if (hits == 0) {
        Logger::Get().Info("StabilityFix",
            "Heap free-list guards fired 0 times this session; the game never "
            "reached the state that crashes unpatched.");
    } else {
        Logger::Get().Info("StabilityFix",
            "Heap free-list guards fired {} time(s). Each one is a null "
            "dereference the vanilla allocator would have performed.", hits);
    }
}

bool StabilityFix::Install() {
    const auto& config = Config::Get().Stability();
    if (!config.fixHeapFreeList) {
        Logger::Get().Info("StabilityFix", "Heap free-list fix disabled by config.");
        return true;
    }

    Logger::Get().Info("StabilityFix", "Guarding heap free-list routines...");

    const bool insertOk = RedirectFunction("Free-list insert (sub_5EEBF0)",
        kFreeListInsert, kInsertVanilla, sizeof(kInsertVanilla),
        reinterpret_cast<void*>(&Hook_FreeListInsert));

    const bool unlinkOk = RedirectFunction("Free-list unlink (sub_5EECA0)",
        kFreeListUnlink, kUnlinkVanilla, sizeof(kUnlinkVanilla),
        reinterpret_cast<void*>(&Hook_FreeListUnlink));

    if (insertOk && unlinkOk) {
        Logger::Get().Info("StabilityFix",
            "  Both routines now null-check their list neighbours before writing "
            "through them.");
        return true;
    }

    // Each routine is self-contained, so half the fix still leaves a consistent
    // game -- but say so plainly rather than reporting success.
    Logger::Get().Warn("StabilityFix",
        "Only part of the heap free-list fix applied (insert={}, unlink={}). The "
        "unpatched routine keeps its original behaviour.", insertOk, unlinkOk);
    return false;
}

} // namespace BullyDE

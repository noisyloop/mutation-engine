#pragma once
#include "MutationState.h"
#include <vector>

namespace me
{
// Snapshot history with undo/redo. Index always points at the "current"
// snapshot. Undo first commits manual tweaks made since the last snapshot,
// so nothing the user dialled in by hand is silently lost.
class UndoStack
{
public:
    explicit UndoStack (size_t capacity = 64) : cap (capacity < 2 ? 2 : capacity) {}

    void clear() { h.clear(); idx = 0; }
    size_t size() const { return h.size(); }
    size_t index() const { return idx; }
    bool canUndo() const { return ! h.empty() && idx > 0; }
    bool canRedo() const { return ! h.empty() && idx + 1 < h.size(); }

    void commit (const MutationState& s)
    {
        if (! h.empty() && h[idx].nearlyEqual (s)) return;
        if (! h.empty()) h.erase (h.begin() + (long) idx + 1, h.end());
        h.push_back (s);
        if (h.size() > cap) h.erase (h.begin());
        idx = h.size() - 1;
    }

    bool undo (const MutationState& live, MutationState& out)
    {
        if (h.empty()) return false;
        if (! h[idx].nearlyEqual (live)) commit (live);
        if (idx == 0) return false;
        out = h[--idx];
        return true;
    }

    bool redo (MutationState& out)
    {
        if (! canRedo()) return false;
        out = h[++idx];
        return true;
    }

private:
    size_t cap, idx = 0;
    std::vector<MutationState> h;
};
} // namespace me

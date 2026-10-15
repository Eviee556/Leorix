#include "core/Undo.h"

void UndoStack::Push(UndoEntry entry) {
    // drop the redo tail
    while ((int)entries.size() > index) {
        bytes -= entries.back().bytes;
        entries.pop_back();
    }
    bytes += entry.bytes;
    entries.push_back(std::move(entry));
    index = (int)entries.size();
    Trim();
}

void UndoStack::Trim() {
    // Oldest entries go first once the history is over budget. Never drop the newest one.
    while ((bytes > budget || entries.size() > maxEntries) && entries.size() > 1) {
        bytes -= entries.front().bytes;
        entries.erase(entries.begin());
        if (index > 0) --index;
    }
}

bool UndoStack::Undo() {
    if (!CanUndo()) return false;
    --index;
    if (entries[index].undo) entries[index].undo();
    return true;
}

bool UndoStack::Redo() {
    if (!CanRedo()) return false;
    if (entries[index].redo) entries[index].redo();
    ++index;
    return true;
}

void UndoStack::Clear() { entries.clear(); index = 0; bytes = 0; }

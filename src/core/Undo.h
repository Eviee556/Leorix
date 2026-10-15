#pragma once
// Undo/redo. Each entry carries the two closures that move the document between its
// before and after state, plus how much memory it holds so the history can be capped.
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct UndoEntry {
    std::string           label;
    std::function<void()> undo;
    std::function<void()> redo;
    size_t                bytes = 0;
};

class UndoStack {
public:
    // Anything past the current position is discarded, the way every editor behaves.
    void Push(UndoEntry entry);
    void Push(const std::string& label, std::function<void()> undo, std::function<void()> redo, size_t bytes = 0) {
        Push(UndoEntry{label, std::move(undo), std::move(redo), bytes});
    }
    bool CanUndo() const { return index > 0; }
    bool CanRedo() const { return index < (int)entries.size(); }
    const char* UndoLabel() const { return CanUndo() ? entries[index - 1].label.c_str() : ""; }
    const char* RedoLabel() const { return CanRedo() ? entries[index].label.c_str() : ""; }
    bool Undo();
    bool Redo();
    void Clear();

    size_t Bytes() const { return bytes; }
    size_t Count() const { return entries.size(); }
    int    Position() const { return index; }
    const std::vector<UndoEntry>& Entries() const { return entries; }

    size_t budget = 512ull * 1024 * 1024;   // history memory cap
    size_t maxEntries = 200;

private:
    void Trim();
    std::vector<UndoEntry> entries;
    int    index = 0;      // number of applied entries
    size_t bytes = 0;
};

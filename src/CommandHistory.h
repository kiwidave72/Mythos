#pragma once
#include <functional>
#include <string>
#include <vector>

// ---- Command ---------------------------------------------------------------
// A reversible editor action. exec() performs it, undo() reverses it.
// Both are called exactly once per use — exec on first execute/redo,
// undo on undo. Capture everything needed by value.
struct Command
{
    std::string          name;   // shown in history panel
    std::function<void()> exec;
    std::function<void()> undo;
};

// ---- CommandHistory --------------------------------------------------------
// Linear undo/redo stack. execute() discards any redo tail (standard behaviour).
// Max depth is capped to avoid unbounded memory growth.
class CommandHistory
{
public:
    static constexpr int kMaxDepth = 100;

    // Execute a command immediately and push it onto the stack.
    // Clears any redo tail beyond the current cursor.
    void execute(Command cmd)
    {
        // Trim redo tail
        if (m_cursor < (int)m_stack.size())
            m_stack.erase(m_stack.begin() + m_cursor, m_stack.end());

        cmd.exec();
        m_stack.push_back(std::move(cmd));

        // Cap depth — drop oldest entries
        while ((int)m_stack.size() > kMaxDepth) {
            m_stack.erase(m_stack.begin());
        }
        m_cursor = (int)m_stack.size();
    }

    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < (int)m_stack.size(); }

    void undo()
    {
        if (!canUndo()) return;
        --m_cursor;
        m_stack[m_cursor].undo();
    }

    void redo()
    {
        if (!canRedo()) return;
        m_stack[m_cursor].exec();
        ++m_cursor;
    }

    void clear()
    {
        m_stack.clear();
        m_cursor = 0;
    }

    // For the history panel UI — entries newest-first
    const std::vector<Command>& stack()  const { return m_stack; }
    int                         cursor() const { return m_cursor; }

private:
    std::vector<Command> m_stack;
    int                  m_cursor = 0;  // points AFTER last executed command
};

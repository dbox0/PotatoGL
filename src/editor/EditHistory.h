#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ComponentRegistry;
class Scene;

struct EditContext
{
    Scene& scene;
    const ComponentRegistry& components;
};

// One undoable edit. Commands refer to objects by Uuid, never by entt::entity, so they
// stay valid when an undo recreates a deleted object. Both return false if the edit no
// longer applies (its object is gone); the history then drops it.
class EditCommand
{
public:
    virtual ~EditCommand() = default;
    virtual bool undo(EditContext& context) = 0;
    virtual bool redo(EditContext& context) = 0;
    virtual std::string name() const = 0;
};

// Every step also remembers the selection (as uuids) before and after it, so undo and
// redo restore what was selected. A selection change with no edit is a step of its own.
class EditHistory
{
private:
    static constexpr std::size_t MaxSteps = 256;

    struct Step
    {
        std::unique_ptr<EditCommand> command;  // nullptr: selection change only
        std::string name;                      // for selection-only steps
        std::uint64_t selectionBefore = 0;
        std::uint64_t selectionAfter = 0;
    };

    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    std::size_t m_pendingSteps = 0;  // pushed since the last endFrame()

    void pushStep(Step step);

public:
    // Runs the command, then records it if it applied
    bool execute(EditContext& context, std::unique_ptr<EditCommand> command);
    // Records a command whose edit already happened (a finished drag, a created object)
    void push(std::unique_ptr<EditCommand> command);

    // Once per frame: the selection at the last call and now. Fills in the selection of
    // the steps pushed since then, or records a selection-only step if it changed.
    void endFrame(std::uint64_t selectionBefore, std::uint64_t selectionAfter, std::string selectName);

    // Return the selection to restore (0 = none), or nullopt if there was nothing to do
    std::optional<std::uint64_t> undo(EditContext& context);
    std::optional<std::uint64_t> redo(EditContext& context);
    void clear();

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::string undoName() const;
    std::string redoName() const;
};

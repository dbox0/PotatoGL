#include "EditHistory.h"

#include <algorithm>

bool EditHistory::execute(EditContext& context, std::unique_ptr<EditCommand> command)
{
    if (!command || !command->redo(context))
        return false;
    push(std::move(command));
    return true;
}

void EditHistory::push(std::unique_ptr<EditCommand> command)
{
    if (command)
        pushStep({std::move(command), {}, {}, {}});
}

void EditHistory::pushStep(Step step)
{
    step.id = m_nextId++;
    m_redo.clear();
    m_undo.push_back(std::move(step));
    ++m_pendingSteps;
    if (m_undo.size() > MaxSteps)
        m_undo.erase(m_undo.begin());
    m_pendingSteps = std::min(m_pendingSteps, m_undo.size());
}

void EditHistory::endFrame(const SelectionIds& selectionBefore, const SelectionIds& selectionAfter,
                           std::string selectName)
{
    if (m_pendingSteps > 0) {
        const std::size_t first = m_undo.size() - m_pendingSteps;
        for (std::size_t i = first; i < m_undo.size(); ++i) {
            m_undo[i].selectionBefore = i == first ? selectionBefore : selectionAfter;
            m_undo[i].selectionAfter = selectionAfter;
        }
    } else if (selectionBefore != selectionAfter) {
        pushStep({nullptr, std::move(selectName), selectionBefore, selectionAfter});
    }
    m_pendingSteps = 0;
}

std::optional<SelectionIds> EditHistory::undo(EditContext& context)
{
    while (!m_undo.empty()) {
        Step step = std::move(m_undo.back());
        m_undo.pop_back();
        m_pendingSteps = std::min(m_pendingSteps, m_undo.size());
        if (!step.command || step.command->undo(context)) {
            SelectionIds selection = step.selectionBefore;
            m_redo.push_back(std::move(step));
            return selection;
        }
    }
    return std::nullopt;
}

std::optional<SelectionIds> EditHistory::redo(EditContext& context)
{
    while (!m_redo.empty()) {
        Step step = std::move(m_redo.back());
        m_redo.pop_back();
        if (!step.command || step.command->redo(context)) {
            SelectionIds selection = step.selectionAfter;
            m_undo.push_back(std::move(step));
            return selection;
        }
    }
    return std::nullopt;
}

std::uint64_t EditHistory::stateId() const
{
    for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it) {
        if (it->command)
            return it->id;
    }
    return 0;
}

void EditHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
    m_pendingSteps = 0;
}

std::string EditHistory::undoName() const
{
    if (m_undo.empty())
        return {};
    const Step& step = m_undo.back();
    return step.command ? step.command->name() : step.name;
}

std::string EditHistory::redoName() const
{
    if (m_redo.empty())
        return {};
    const Step& step = m_redo.back();
    return step.command ? step.command->name() : step.name;
}

#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <utility>

namespace tf::app {
template <typename State>
class SnapshotAction final : public juce::UndoableAction
{
public:
    SnapshotAction(State beforeState, State afterState, std::function<void(const State&)> applyFn)
        : before(std::move(beforeState)), after(std::move(afterState)), apply(std::move(applyFn))
    {
    }

    bool perform() override
    {
        if (applied)
            apply(after);
        applied = true;
        return true;
    }

    bool undo() override
    {
        apply(before);
        return true;
    }

    int getSizeInUnits() override { return 10; }

private:
    State before, after;
    std::function<void(const State&)> apply;
    bool applied = false;
};

class ParamAction final : public juce::UndoableAction
{
public:
    ParamAction(int paramIndex, float oldValue, float newValue, std::function<void(int, float)> applyFn)
        : param(paramIndex), from(oldValue), to(newValue), apply(std::move(applyFn))
    {
    }

    bool perform() override
    {
        if (applied)
            apply(param, to);
        applied = true;
        return true;
    }

    bool undo() override
    {
        apply(param, from);
        return true;
    }

    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override
    {
        if (auto* p = dynamic_cast<ParamAction*>(next); p != nullptr && p->param == param)
        {
            auto* merged = new ParamAction(param, from, p->to, apply);
            merged->applied = true;
            return merged;
        }
        return nullptr;
    }

    int getSizeInUnits() override { return 1; }

private:
    int param;
    float from, to;
    std::function<void(int, float)> apply;
    bool applied = false;
};
}

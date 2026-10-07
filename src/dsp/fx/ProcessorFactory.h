#pragma once

#include "Processor.h"

#include <string_view>
#include <vector>

namespace tf::dsp {

/** Registry of processor types that can be loaded into an FX slot. Creating a
    processor allocates, so this is message-thread only.

    Imported processors (the reverse shimmer and fuzz) register here too, from the
    JUCE-based fx library, without the slot or engine code changing. */
class ProcessorFactory
{
public:
    using CreateFn = ProcessorPtr (*)();

    struct Entry
    {
        const ProcessorInfo* info;
        CreateFn create;
    };

    /** The built-in set plus anything registered at startup. */
    static ProcessorFactory& instance();

    void add(const ProcessorInfo& info, CreateFn createFn);

    /** nullptr for an unknown id or the empty id (an empty slot). */
    ProcessorPtr create(std::string_view typeId) const;
    const ProcessorInfo* find(std::string_view typeId) const noexcept;
    const std::vector<Entry>& entries() const noexcept { return list; }

private:
    ProcessorFactory();
    std::vector<Entry> list;
};

} // namespace tf::dsp

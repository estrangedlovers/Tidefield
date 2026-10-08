#pragma once

#include "Processor.h"

#include <string_view>
#include <vector>

namespace tf::dsp {
class ProcessorFactory
{
public:
    using CreateFn = ProcessorPtr (*)();

    struct Entry
    {
        const ProcessorInfo* info;
        CreateFn create;
    };

    static ProcessorFactory& instance();

    void add(const ProcessorInfo& info, CreateFn createFn);

    ProcessorPtr create(std::string_view typeId) const;
    const ProcessorInfo* find(std::string_view typeId) const noexcept;
    const std::vector<Entry>& entries() const noexcept { return list; }

private:
    ProcessorFactory();
    std::vector<Entry> list;
};
}

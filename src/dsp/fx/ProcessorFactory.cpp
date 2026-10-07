#include "ProcessorFactory.h"

#include "chorus/Ensemble.h"
#include "delay/TapeDelay.h"
#include "delay/WornEcho.h"
#include "medium/Medium.h"
#include "reverb/FdnReverb.h"
#include "spectral/SpectralBlur.h"
#include "strings/SympatheticStrings.h"

namespace tf::dsp {

ProcessorFactory& ProcessorFactory::instance()
{
    static ProcessorFactory factory;
    return factory;
}

ProcessorFactory::ProcessorFactory()
{
    add(FdnReverb::kInfo, [] { return ProcessorPtr(new FdnReverb()); });
    add(TapeDelay::kInfo, [] { return ProcessorPtr(new TapeDelay()); });
    add(MediumProcessor::kInfo, [] { return ProcessorPtr(new MediumProcessor()); });
    add(WornEcho::kInfo, [] { return ProcessorPtr(new WornEcho()); });
    add(Ensemble::kInfo, [] { return ProcessorPtr(new Ensemble()); });
    add(SpectralBlur::kInfo, [] { return ProcessorPtr(new SpectralBlur()); });
    add(SympatheticStrings::kInfo, [] { return ProcessorPtr(new SympatheticStrings()); });
}

void ProcessorFactory::add(const ProcessorInfo& info, CreateFn createFn)
{
    for (auto& e : list)
        if (std::string_view(e.info->typeId) == info.typeId)
        {
            e = { &info, createFn };
            return;
        }
    list.push_back({ &info, createFn });
}

ProcessorPtr ProcessorFactory::create(std::string_view typeId) const
{
    for (const auto& e : list)
        if (typeId == e.info->typeId)
            return e.create();
    return nullptr;
}

const ProcessorInfo* ProcessorFactory::find(std::string_view typeId) const noexcept
{
    for (const auto& e : list)
        if (typeId == e.info->typeId)
            return e.info;
    return nullptr;
}

} // namespace tf::dsp

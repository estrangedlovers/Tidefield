#include "support/AllocationGuard.h"
#include "support/FakeInstrument.h"

#include <engine/Engine.h>
#include <engine/guest/GuestManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <memory>
#include <vector>

using namespace tf::engine;
using tf::test::FakeInstrument;

namespace {
constexpr double kRate = 48000.0;

struct GuestRig
{
    explicit GuestRig(int block = 256) : blockSize(block)
    {
        engine.prepare(kRate, block);
        engine.setParam(P::MasterFadeSecs, 0.5f);
        engine.command(Command::FadeIn);
        for (const auto& s : kStrips)
            snap(s.level, -60.0f);
        snap(P::GuestLevel, 0.0f);
        snap(P::BusALevel, -60.0f);
        snap(P::BusBLevel, -60.0f);
        l.assign(static_cast<std::size_t>(block), 0.0f);
        r.assign(static_cast<std::size_t>(block), 0.0f);
    }

    void snap(P p, float v) { engine.post(ControlEvent::snapParam(idx(p), v)); }

    FakeInstrument* load()
    {
        auto fake = std::make_unique<FakeInstrument>();
        fake->prepare(engine.getGuestSpec());
        auto* raw = fake.get();
        REQUIRE(engine.sendInstrument(std::move(fake)));
        return raw;
    }

    void run(double seconds)
    {
        const int total = static_cast<int>(seconds * kRate);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            float* outs[2] = { l.data(), r.data() };
            engine.process(nullptr, 0, outs, 2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                peak = std::max(peak, std::fabs(l[static_cast<std::size_t>(i)]));
                out.push_back(l[static_cast<std::size_t>(i)]);
            }
            TelemetryFrame f;
            while (engine.popTelemetry(f))
                last = f;
            EngineNotice n;
            while (engine.popNotice(n)) {}
            MidiOutEvent m;
            while (engine.popMidiOut(m))
                midi.push_back(m);
            engine.collectInstruments();
            engine.collectGarbage();
        }
    }

    void resetPeak() { peak = 0.0f; }

    Engine engine;
    int blockSize;
    std::vector<float> l, r, out;
    std::vector<MidiOutEvent> midi;
    TelemetryFrame last;
    float peak = 0.0f;
};

void cyclesFast(Engine& e)
{
    e.setParam(P::LoopsOn, 1.0f);
    e.setParam(P::LoopsRate, 4.0f);
    e.setParam(P::TideRate, 8.0f);
    e.setParam(P::LoopsDensity, 1.0f);
    e.setParam(P::LoopsTarget, 3.0f);
}

struct FakeProvider final : ExternalInstruments
{
    bool handlesInstrument(std::string_view typeId) const override { return typeId.starts_with("fake:"); }
    const InstrumentInfo* findInstrument(std::string_view typeId) const override { return typeId == "fake:sine" ? &info : nullptr; }
    InstrumentPtr createInstrument(std::string_view, const std::string& state, const tf::dsp::ProcessSpec& spec) override
    {
        createdWith = state;
        ++created;
        auto fake = std::make_unique<FakeInstrument>();
        fake->prepare(spec);
        live = fake.get();
        return fake;
    }
    std::string captureInstrumentState() const override { return live != nullptr ? "live" : std::string(); }

    InstrumentInfo info { "fake:sine", "Fake Sine", { "Level", "Bright", "Pan", "", "", "" }, { 0.8f, 0.2f, 0.5f, 0.5f, 0.5f, 0.5f } };
    std::string createdWith;
    int created = 0;
    FakeInstrument* live = nullptr;
};
}

TEST_CASE("A loaded guest instrument plays keyboard notes through its strip")
{
    GuestRig rig;
    rig.run(0.6);
    REQUIRE(rig.peak < 1.0e-6f);
    auto* fake = rig.load();
    rig.run(0.1);
    REQUIRE(rig.last.guestLoaded);
    rig.engine.setParam(P::GuestP1, 1.0f);
    rig.engine.noteOn(60, 0.8f);
    rig.run(0.5);
    REQUIRE(fake->held() == 1);
    REQUIRE(fake->noteOns() == 1);
    REQUIRE(fake->controls[0] > 0.9f);
    REQUIRE(fake->preparedBlock == Engine::kGuestBlock);
    REQUIRE(fake->largestBlock == Engine::kGuestBlock);
    REQUIRE(rig.peak > 0.01f);
    REQUIRE(rig.last.guestNotes == 1);
    REQUIRE(rig.last.stripPeakL[static_cast<std::size_t>(StripId::Guest)] > 0.0f);
    rig.engine.noteOff(60);
    rig.run(0.2);
    REQUIRE(fake->held() == 0);
    REQUIRE(rig.last.guestNotes == 0);
}

TEST_CASE("Guest notes keep their relative timing to the sample")
{
    GuestRig rig(64);
    auto* fake = rig.load();
    rig.run(0.2);
    rig.engine.noteOn(60, 0.8f);
    rig.run(3 * 64 / kRate);
    rig.engine.noteOn(64, 0.8f);
    rig.run(0.1);
    REQUIRE(fake->logged == 2);
    REQUIRE(fake->log[1].time - fake->log[0].time == 3u * 64u);
}

TEST_CASE("Transpose shifts guest notes and note-offs still find them")
{
    GuestRig rig;
    auto* fake = rig.load();
    rig.engine.setParam(P::GuestTranspose, 12.0f);
    rig.run(0.1);
    rig.engine.noteOn(60, 0.8f);
    rig.run(0.1);
    rig.engine.setParam(P::GuestTranspose, -5.0f);
    rig.run(0.1);
    rig.engine.noteOff(60);
    rig.run(0.1);
    REQUIRE(fake->logged == 2);
    REQUIRE(fake->log[0].event.data1 == 72);
    REQUIRE(fake->log[1].event.isNoteOff());
    REQUIRE(fake->log[1].event.data1 == 72);
    REQUIRE(fake->held() == 0);
}

TEST_CASE("Play From chooses which notes reach the guest")
{
    SECTION("MIDI input only ignores the on-screen keyboard but keeps the MIDI channel")
    {
        GuestRig rig;
        auto* fake = rig.load();
        rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromMidi));
        rig.run(0.1);
        rig.engine.noteOn(60, 0.8f);
        rig.engine.postMidi(0, { 0x93, 62, 100, 0 });
        rig.engine.postMidi(0, { 0xe3, 0, 80, 0 });
        rig.run(0.2);
        REQUIRE(fake->noteOns() == 1);
        REQUIRE(fake->log[0].event.data1 == 62);
        REQUIRE(fake->log[0].event.channel() == 3);
        REQUIRE(fake->logged == 2);
        REQUIRE(fake->log[1].event.type() == 0xe0);
        rig.engine.postMidi(0, { 0x83, 62, 0, 0 });
        rig.run(0.1);
        REQUIRE(fake->held() == 0);
    }
    SECTION("Bloom's notes include MIDI input")
    {
        GuestRig rig;
        auto* fake = rig.load();
        rig.run(0.1);
        rig.engine.noteOn(60, 0.8f);
        rig.engine.postMidi(0, { 0x90, 62, 100, 0 });
        rig.run(0.2);
        REQUIRE(fake->noteOns() == 2);
    }
    SECTION("Cycles hold each note for the note length")
    {
        GuestRig rig;
        auto* fake = rig.load();
        rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromCycles));
        rig.snap(P::LoopsGate, 0.25f);
        cyclesFast(rig.engine);
        rig.run(0.1);
        rig.engine.noteOn(40, 0.8f);
        rig.run(4.0);
        REQUIRE(fake->noteOns() >= 6);
        std::map<int, std::uint64_t> started;
        int lengthsChecked = 0;
        for (int k = 0; k < fake->logged; ++k)
        {
            const auto& e = fake->log[static_cast<std::size_t>(k)];
            REQUIRE(e.event.data1 != 40);
            if (e.event.isNoteOn())
                started[e.event.data1] = e.time;
            else if (e.event.isNoteOff() && started.count(e.event.data1) != 0)
            {
                const double secs = static_cast<double>(e.time - started[e.event.data1]) / kRate;
                CHECK(secs <= 0.26);
                lengthsChecked += secs > 0.24 ? 1 : 0;
                started.erase(e.event.data1);
            }
        }
        REQUIRE(lengthsChecked >= 3);
        rig.engine.setParam(P::LoopsOn, 0.0f);
        rig.run(0.1);
        REQUIRE(fake->held() == 0);
    }
    SECTION("All plays the keyboard and the Cycles")
    {
        GuestRig rig;
        auto* fake = rig.load();
        rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromAll));
        cyclesFast(rig.engine);
        rig.run(0.1);
        rig.engine.noteOn(30, 0.8f);
        rig.run(3.0);
        bool keyed = false;
        for (int k = 0; k < fake->logged; ++k)
            keyed = keyed || (fake->log[static_cast<std::size_t>(k)].event.isNoteOn() && fake->log[static_cast<std::size_t>(k)].event.data1 == 30);
        REQUIRE(keyed);
        REQUIRE(fake->noteOns() >= 4);
    }
}

TEST_CASE("Switching Play From and panic release every guest note")
{
    GuestRig rig;
    auto* fake = rig.load();
    rig.run(0.1);
    rig.engine.noteOn(60, 0.8f);
    rig.engine.noteOn(67, 0.8f);
    rig.run(0.1);
    REQUIRE(fake->held() == 2);
    rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromCycles));
    rig.run(0.1);
    REQUIRE(fake->held() == 0);

    rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromAll));
    rig.run(0.1);
    rig.engine.noteOn(62, 0.8f);
    rig.run(0.1);
    REQUIRE(fake->held() == 1);
    rig.engine.command(Command::Panic);
    rig.run(0.2);
    REQUIRE(fake->held() == 0);
    REQUIRE(fake->resets >= 1);
}

TEST_CASE("Replacing the guest crossfades and retires the old instrument")
{
    GuestRig rig;
    auto* first = rig.load();
    rig.run(0.1);
    rig.engine.noteOn(60, 0.8f);
    rig.run(0.2);
    REQUIRE(first->held() == 1);
    auto* second = rig.load();
    rig.run(0.3);
    REQUIRE(second->blocks > 0);
    REQUIRE(rig.last.guestNotes == 0);
    REQUIRE(rig.engine.sendInstrument(nullptr));
    rig.run(0.3);
    REQUIRE_FALSE(rig.last.guestLoaded);
    rig.resetPeak();
    rig.run(0.3);
    REQUIRE(rig.peak < 1.0e-6f);
}

TEST_CASE("Engine::process never allocates with a guest, Cycles and MIDI out running")
{
    GuestRig rig(512);
    rig.load();
    rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromAll));
    rig.engine.setParam(P::LoopsMidiOut, 1.0f);
    cyclesFast(rig.engine);
    rig.run(0.2);
    std::vector<float> l(512), r(512);
    float* outs[2] = { l.data(), r.data() };
    std::size_t allocations = 0;
    {
        const tf::test::ScopedAllocationCounter counter;
        for (int b = 0; b < 48000 * 4 / 512; ++b)
        {
            if (b % 20 == 0)
                rig.engine.noteOn(50 + b % 24, 0.7f);
            if (b % 20 == 10)
                rig.engine.noteOff(50 + (b - 10) % 24);
            if (b % 37 == 0)
                rig.engine.postMidi(0, { 0x91, static_cast<std::uint8_t>(40 + b % 30), 90, 0 });
            if (b == 150)
                rig.engine.command(Command::Panic);
            rig.engine.process(nullptr, 0, outs, 2, 512);
            MidiOutEvent m;
            while (rig.engine.popMidiOut(m)) {}
        }
        allocations = counter.count();
    }
    REQUIRE(allocations == 0);
}

TEST_CASE("Guest playback from the Cycles is identical across host block sizes")
{
    auto render = [](int block) {
        GuestRig rig(block);
        rig.load();
        rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromCycles));
        rig.engine.setParam(P::LoopsMidiOut, 1.0f);
        cyclesFast(rig.engine);
        rig.run(3.0);
        rig.out.resize(static_cast<std::size_t>(2.9 * kRate));
        return std::make_pair(rig.out, rig.midi);
    };
    const auto [a, ma] = render(512);
    const auto [b, mb] = render(96);
    REQUIRE(a.size() == b.size());
    float peak = 0.0f;
    for (float x : a)
        peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 0.001f);
    std::size_t firstDiff = a.size();
    for (std::size_t k = 0; k < a.size() && firstDiff == a.size(); ++k)
        if (a[k] != b[k])
            firstDiff = k;
    INFO("first difference at sample " << firstDiff);
    REQUIRE(firstDiff == a.size());
    auto early = [](const std::vector<MidiOutEvent>& v) {
        std::size_t n = 0;
        while (n < v.size() && static_cast<double>(v[n].sampleTime) < 2.9 * kRate)
            ++n;
        return n;
    };
    REQUIRE(early(ma) == early(mb));
    REQUIRE(early(ma) > 4);
    for (std::size_t k = 0; k < early(ma); ++k)
    {
        REQUIRE(ma[k].sampleTime == mb[k].sampleTime);
        REQUIRE(ma[k].status == mb[k].status);
        REQUIRE(ma[k].data1 == mb[k].data1);
    }
}

TEST_CASE("Cycles MIDI out sends notes with lengths, channels and clean note-offs")
{
    SECTION("one channel for all")
    {
        GuestRig rig;
        rig.engine.setParam(P::LoopsMidiOut, 1.0f);
        rig.engine.setParam(P::LoopsMidiChannel, 5.0f);
        rig.snap(P::LoopsGate, 0.2f);
        cyclesFast(rig.engine);
        rig.run(3.0);
        int ons = 0;
        std::map<int, std::uint64_t> started;
        for (const auto& m : rig.midi)
        {
            REQUIRE((m.status & 0x0f) == 4);
            if ((m.status & 0xf0) == 0x90)
            {
                ++ons;
                REQUIRE(m.data2 >= 1);
                started[m.data1] = m.sampleTime;
            }
            else if ((m.status & 0xf0) == 0x80 && started.count(m.data1) != 0)
                REQUIRE(static_cast<double>(m.sampleTime - started[m.data1]) / kRate <= 0.21);
        }
        REQUIRE(ons >= 4);
    }
    SECTION("a channel per cycle")
    {
        GuestRig rig;
        rig.engine.setParam(P::LoopsMidiOut, 1.0f);
        cyclesFast(rig.engine);
        rig.run(4.0);
        std::map<int, int> channels;
        for (const auto& m : rig.midi)
            if ((m.status & 0xf0) == 0x90)
                ++channels[m.status & 0x0f];
        REQUIRE(channels.size() >= 3);
        for (const auto& [ch, n] : channels)
            REQUIRE(ch < 8);
    }
    SECTION("stopping the Cycles, turning MIDI out off or panic leaves nothing held")
    {
        for (int how = 0; how < 3; ++how)
        {
            GuestRig rig;
            rig.engine.setParam(P::LoopsMidiOut, 1.0f);
            rig.snap(P::LoopsGate, 8.0f);
            cyclesFast(rig.engine);
            rig.run(2.5);
            REQUIRE(rig.last.cycleMidiNotes > 0);
            if (how == 0)
                rig.engine.setParam(P::LoopsOn, 0.0f);
            else if (how == 1)
                rig.engine.setParam(P::LoopsMidiOut, 0.0f);
            else
                rig.engine.command(Command::Panic);
            rig.run(0.3);
            REQUIRE(rig.last.cycleMidiNotes == 0);
            std::map<int, int> held;
            for (const auto& m : rig.midi)
            {
                const int key = (m.status & 0x0f) * 128 + m.data1;
                if ((m.status & 0xf0) == 0x90)
                    ++held[key];
                else if ((m.status & 0xf0) == 0x80)
                    held[key] = 0;
                else if ((m.status & 0xf0) == 0xb0 && m.data1 == 123)
                    for (auto& [k, n] : held)
                        if (k / 128 == (m.status & 0x0f))
                            n = 0;
            }
            for (const auto& [k, n] : held)
                REQUIRE(n == 0);
        }
    }
    SECTION("nothing is sent while MIDI out is off")
    {
        GuestRig rig;
        cyclesFast(rig.engine);
        rig.run(3.0);
        REQUIRE(rig.midi.empty());
    }
}

TEST_CASE("GuestManager loads, restores and keeps a missing instrument")
{
    GuestRig rig;
    FakeProvider provider;
    GuestManager guest(rig.engine);
    guest.setExternal(&provider);

    guest.setType("fake:sine");
    REQUIRE(provider.created == 1);
    REQUIRE(guest.getName() == "Fake Sine");
    REQUIRE_FALSE(guest.isMissing());
    rig.run(0.5);
    guest.tick();
    REQUIRE(rig.last.guestLoaded);
    REQUIRE(provider.live->controls[0] > 0.75f);
    REQUIRE(guest.getState() == "live");

    guest.setType("fake:gone", false, "saved-state", "Gone Synth");
    REQUIRE(guest.isMissing());
    REQUIRE(guest.getType() == "fake:gone");
    REQUIRE(guest.getState() == "saved-state");
    REQUIRE(guest.getName() == "Gone Synth");
    rig.run(0.2);
    guest.tick();
    REQUIRE_FALSE(rig.last.guestLoaded);

    guest.setType("fake:sine", false, "restore-me");
    REQUIRE(provider.createdWith == "restore-me");
    guest.clear();
    REQUIRE(guest.isEmpty());
    REQUIRE(guest.getState().empty());
    rig.run(0.2);
    guest.tick();
    REQUIRE_FALSE(rig.last.guestLoaded);

    GuestManager none(rig.engine);
    none.setType("plugin:VST3-Something", false, "kept", "Something");
    REQUIRE(none.isMissing());
    REQUIRE(none.getState() == "kept");
}

namespace {
int lastNoteState(const FakeInstrument& fake, int note)
{
    int sounding = 0;
    for (int k = 0; k < fake.logged; ++k)
    {
        const auto& e = fake.log[static_cast<std::size_t>(k)].event;
        if (e.data1 == note && (e.type() == 0x80 || e.type() == 0x90))
            sounding = e.isNoteOn() ? 1 : 0;
    }
    return sounding;
}

int firstNoteOn(const FakeInstrument& fake, int from = 0)
{
    for (int k = from; k < fake.logged; ++k)
        if (fake.log[static_cast<std::size_t>(k)].event.isNoteOn())
            return fake.log[static_cast<std::size_t>(k)].event.data1;
    return -1;
}

void oneCycle(GuestRig& rig, float gate)
{
    rig.engine.setParam(P::GuestPlayFrom, static_cast<float>(Engine::kFromAll));
    rig.snap(P::LoopsCount, 1.0f);
    rig.snap(P::LoopsSpread, 0.0f);
    rig.snap(P::LoopsGate, gate);
    cyclesFast(rig.engine);
}
}

TEST_CASE("A note-off from one origin never ends a Guest note another origin still holds")
{
    SECTION("the keyboard and MIDI input on channel 1")
    {
        GuestRig rig;
        auto* fake = rig.load();
        rig.run(0.1);
        rig.engine.noteOn(60, 0.8f);
        rig.run(0.1);
        rig.engine.postMidi(0, { 0x90, 60, 100, 0 });
        rig.run(0.1);
        REQUIRE(fake->noteOns() == 2);
        REQUIRE(fake->held() == 1);
        rig.engine.noteOff(60);
        rig.run(0.1);
        REQUIRE(fake->held() == 1);
        REQUIRE(lastNoteState(*fake, 60) == 1);
        rig.engine.postMidi(0, { 0x80, 60, 0, 0 });
        rig.run(0.1);
        REQUIRE(fake->held() == 0);
    }
    SECTION("a keyboard note outlives the Cycles playing and releasing the same note")
    {
        GuestRig rig;
        auto* fake = rig.load();
        oneCycle(rig, 0.1f);
        rig.run(2.0);
        const int cycleNote = firstNoteOn(*fake);
        REQUIRE(cycleNote >= 0);
        rig.engine.setParam(P::LoopsOn, 0.0f);
        rig.run(0.5);
        REQUIRE(fake->held() == 0);

        const int before = fake->logged;
        rig.engine.noteOn(cycleNote, 0.8f);
        rig.run(0.1);
        rig.engine.setParam(P::LoopsOn, 1.0f);
        rig.run(3.0);
        rig.engine.setParam(P::LoopsOn, 0.0f);
        rig.run(0.5);
        int repeats = 0;
        for (int k = before; k < fake->logged; ++k)
        {
            const auto& e = fake->log[static_cast<std::size_t>(k)].event;
            repeats += e.isNoteOn() && e.data1 == cycleNote ? 1 : 0;
        }
        REQUIRE(repeats >= 3);
        REQUIRE(fake->held() == 1);
        REQUIRE(lastNoteState(*fake, cycleNote) == 1);
        rig.engine.noteOff(cycleNote);
        rig.run(0.1);
        REQUIRE(fake->held() == 0);
    }
    SECTION("a Cycles note outlives a keyboard release of the same note")
    {
        GuestRig rig;
        auto* fake = rig.load();
        oneCycle(rig, 8.0f);
        rig.run(2.0);
        const int cycleNote = firstNoteOn(*fake);
        REQUIRE(cycleNote >= 0);
        REQUIRE(fake->held() == 1);
        rig.engine.noteOn(cycleNote, 0.8f);
        rig.run(0.05);
        rig.engine.noteOff(cycleNote);
        rig.run(0.05);
        REQUIRE(fake->held() == 1);
        REQUIRE(lastNoteState(*fake, cycleNote) == 1);
        rig.engine.setParam(P::LoopsOn, 0.0f);
        rig.run(0.2);
        REQUIRE(fake->held() == 0);
    }
}

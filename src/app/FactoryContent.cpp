#include "FactoryContent.h"

#include <BinaryData.h>
#include <io/AudioFileIO.h>

#include <map>

namespace tf::app {
const std::vector<FactorySound>& factorySounds()
{
    static const std::vector<FactorySound> sounds {
        { "Glass", "Tonal", "glass_wav", 81 },
        { "Singing bowl", "Tonal", "bowl_wav", 57 },
        { "Kalimba", "Tonal", "kalimba_wav", 72 },
        { "Felt piano", "Tonal", "felt_piano_wav", 60 },
        { "Marimba", "Tonal", "marimba_wav", 60 },
        { "Bell", "Tonal", "bell_wav", 69 },
        { "Pluck", "Tonal", "pluck_wav", 50 },
        { "Vibraphone", "Tonal", "vibraphone_wav", 65 },
        { "Glass harmonica", "Tonal", "glass_harmonica_wav", 72 },
        { "Music box", "Tonal", "music_box_wav", 84 },
        { "Celesta", "Tonal", "celesta_wav", 72 },
        { "Harp", "Tonal", "harp_wav", 55 },
        { "Koto", "Tonal", "koto_wav", 62 },
        { "Tongue drum", "Tonal", "tongue_drum_wav", 62 },
        { "Electric piano", "Tonal", "electric_piano_wav", 60 },
        { "Bonang", "Tonal", "bonang_wav", 63 },
        { "Temple bell", "Tonal", "temple_bell_wav", 43 },
        { "Crystal bowl", "Tonal", "crystal_bowl_wav", 60 },
        { "Hang drum", "Tonal", "hang_drum_wav", 57 },
        { "Glockenspiel", "Tonal", "glockenspiel_wav", 84 },
        { "Dulcimer", "Tonal", "dulcimer_wav", 62 },
        { "Prepared piano", "Tonal", "prepared_piano_wav", 55 },
        { "Gong", "Tonal", "gong_wav", 45 },
        { "Lyre", "Tonal", "lyre_wav", 57 },
        { "Bowed vibraphone", "Tonal", "bowed_vibraphone_wav", 65 },
        { "Chord", "Pad", "chord_wav", 50 },
        { "Choir", "Pad", "choir_wav", 50 },
        { "Bowed strings", "Pad", "bowed_strings_wav", 38 },
        { "Harmonium", "Pad", "harmonium_wav", 60 },
        { "Sub organ", "Pad", "sub_organ_wav", 38 },
        { "Shimmer", "Pad", "shimmer_wav", 74 },
        { "Warm analog", "Pad", "warm_analog_wav", 48 },
        { "String ensemble", "Pad", "string_ensemble_wav", 43 },
        { "Airy voices", "Pad", "airy_voices_wav", 57 },
        { "Reed organ", "Pad", "reed_organ_wav", 53 },
        { "Glass pad", "Pad", "glass_pad_wav", 64 },
        { "Cello section", "Pad", "cello_section_wav", 36 },
        { "Chamber choir", "Pad", "chamber_choir_wav", 40 },
        { "Warped tape", "Pad", "warped_tape_wav", 50 },
        { "Drifting pad", "Pad", "drifting_pad_wav", 48 },
        { "Frost", "Pad", "frost_wav", 64 },
        { "Hollow fifths", "Pad", "hollow_fifths_wav", 43 },
        { "Vowel morph", "Pad", "vowel_morph_wav", 52 },
        { "Midnight pad", "Pad", "midnight_pad_wav", 45 },
        { "Tanpura", "Drone", "tanpura_wav", 48 },
        { "Cello drone", "Drone", "cello_drone_wav", 36 },
        { "Bowed metal", "Drone", "bowed_metal_wav", 57 },
        { "Organ pedal", "Drone", "organ_pedal_wav", 24 },
        { "Sub hum", "Drone", "sub_hum_wav", 28 },
        { "Shruti box", "Drone", "shruti_box_wav", 48 },
        { "Bowed glass", "Drone", "bowed_glass_wav", 67 },
        { "Low brass", "Drone", "low_brass_wav", 34 },
        { "Overtone choir", "Drone", "overtone_choir_wav", 45 },
        { "Granular hum", "Drone", "granular_hum_wav", 40 },
        { "Hurdy-gurdy", "Drone", "hurdy_gurdy_wav", 43 },
        { "Analog drone", "Drone", "analog_drone_wav", 38 },
        { "Wind chimes", "Texture", "wind_chimes_wav", -1 },
        { "Breath", "Texture", "breath_wav", -1 },
        { "Ocean", "Texture", "ocean_wav", -1 },
        { "Rain on leaves", "Texture", "rain_leaves_wav", -1 },
        { "Tape dust", "Texture", "tape_dust_wav", -1 },
        { "Forest at dawn", "Texture", "forest_dawn_wav", -1 },
        { "Stream", "Texture", "stream_wav", -1 },
        { "Distant thunder", "Texture", "distant_thunder_wav", -1 },
        { "Radio static", "Texture", "radio_static_wav", -1 },
        { "Vinyl crackle", "Texture", "vinyl_crackle_wav", -1 },
        { "Fire", "Texture", "fire_crackle_wav", -1 },
        { "Night insects", "Texture", "night_insects_wav", -1 },
        { "Wind in wires", "Texture", "wind_wires_wav", -1 },
        { "Rain on a tin roof", "Texture", "rain_tin_roof_wav", -1 },
        { "Underwater", "Texture", "underwater_wav", -1 },
        { "Snowfall", "Texture", "snowfall_wav", -1 },
        { "Cave drips", "Texture", "cave_drips_wav", -1 },
        { "Harbour", "Texture", "harbour_wav", -1 },
        { "Distant bells", "Texture", "distant_bells_wav", -1 },
        { "Rain on glass", "Texture", "rain_glass_wav", -1 },
        { "Pine wind", "Texture", "pine_wind_wav", -1 },
        { "Frozen lake", "Texture", "frozen_lake_wav", -1 },
        { "Wood knock", "One-shot", "wood_knock_wav", 74 },
        { "Bowl strike", "One-shot", "bowl_strike_wav", 62 },
        { "Piano harmonic", "One-shot", "piano_harmonic_wav", 60 },
        { "Metal scrape", "One-shot", "metal_scrape_wav", -1 },
        { "Breath swell", "One-shot", "breath_swell_wav", -1 },
        { "Vocal swell", "One-shot", "vocal_swell_wav", 57 },
        { "Felt mallet", "One-shot", "felt_mallet_wav", 48 },
        { "Reverse bell", "One-shot", "reverse_bell_wav", 69 },
        { "Bowed cymbal", "One-shot", "bowed_cymbal_wav", -1 },
        { "Rain stick", "One-shot", "rain_stick_wav", -1 },
        { "Breath flute", "One-shot", "breath_flute_wav", 67 },
        { "Sub bloom", "One-shot", "sub_bloom_wav", 36 },
    };
    return sounds;
}

std::shared_ptr<const dsp::SampleBuffer> loadFactorySound(const FactorySound& sound)
{
    int size = 0;
    const char* data = BinaryData::getNamedResource(sound.resource, size);
    if (data == nullptr || size <= 0)
        return nullptr;
    juce::String error;
    auto b = io::loadSample(std::make_unique<juce::MemoryInputStream>(data, static_cast<size_t>(size), false), sound.name, error);
    return std::shared_ptr<const dsp::SampleBuffer>(std::move(b));
}

namespace {
const FactorySound* findSound(const char* name)
{
    for (const auto& s : factorySounds())
        if (juce::String(s.name) == name)
            return &s;
    return nullptr;
}

io::SessionData::SceneData scene(const char* name, float x, float y, std::map<std::string, float> values)
{
    io::SessionData::SceneData s;
    s.name = name;
    s.x = x;
    s.y = y;
    s.values = std::move(values);
    return s;
}
}

io::SessionData makeStarterSession(const engine::Engine& engine)
{
    auto s = io::defaultSession(engine);

    if (const auto* choir = findSound("Choir"))
        if (auto b = loadFactorySound(*choir))
            s.samples["cloud1"] = b;
    if (const auto* bowl = findSound("Singing bowl"))
        if (auto b = loadFactorySound(*bowl))
            s.samples["cloud2"] = b;
    if (const auto* glass = findSound("Glass"))
        if (auto b = loadFactorySound(*glass))
        {
            s.samples["bloom"] = b;
            s.params["bloom.root"] = static_cast<float>(glass->rootNote);
        }
    s.params["cloud2.level"] = -60.0f;

    s.scenes = {
        scene("Still", 0.18f, 0.78f, { { "drone.cutoff", 700.0f }, { "drone.density", 2.0f }, { "drone.level", -4.0f }, { "drone.noise", 0.05f },
                                       { "cloud1.density", 5.0f }, { "cloud1.grainMs", 700.0f }, { "cloud1.spray", 0.1f }, { "cloud1.level", -6.0f },
                                       { "cloud2.level", -60.0f }, { "cloud2.density", 4.0f }, { "res.rain", 0.05f }, { "res.level", -14.0f },
                                       { "weather.wind", 0.0f }, { "weather.rain", 0.0f }, { "weather.surf", 0.0f }, { "medium.drive", 0.2f },
                                       { "tide.rate", 0.6f }, { "drone.sendA", -12.0f } }),
        scene("Dawn", 0.5f, 0.86f, { { "drone.cutoff", 1600.0f }, { "drone.density", 3.0f }, { "drone.level", -3.0f }, { "drone.noise", 0.12f },
                                     { "cloud1.density", 14.0f }, { "cloud1.grainMs", 320.0f }, { "cloud1.spray", 0.3f }, { "cloud1.level", -3.0f },
                                     { "cloud2.level", -14.0f }, { "cloud2.density", 6.0f }, { "res.rain", 0.18f }, { "res.level", -8.0f },
                                     { "weather.wind", 0.08f }, { "weather.rain", 0.0f }, { "weather.surf", 0.0f }, { "medium.drive", 0.25f },
                                     { "tide.rate", 1.0f }, { "drone.sendA", -10.0f } }),
        scene("Glass rain", 0.84f, 0.74f, { { "drone.cutoff", 2600.0f }, { "drone.density", 3.0f }, { "drone.level", -10.0f }, { "drone.noise", 0.1f },
                                            { "cloud1.density", 30.0f }, { "cloud1.grainMs", 120.0f }, { "cloud1.spray", 0.6f }, { "cloud1.level", -8.0f },
                                            { "cloud2.level", -6.0f }, { "cloud2.density", 18.0f }, { "res.rain", 0.7f }, { "res.level", -3.0f },
                                            { "weather.wind", 0.05f }, { "weather.rain", 0.3f }, { "weather.surf", 0.0f }, { "medium.drive", 0.3f },
                                            { "tide.rate", 1.6f }, { "drone.sendA", -8.0f } }),
        scene("Tidepool", 0.5f, 0.48f, { { "drone.cutoff", 1100.0f }, { "drone.density", 4.0f }, { "drone.level", -4.0f }, { "drone.noise", 0.15f },
                                         { "cloud1.density", 22.0f }, { "cloud1.grainMs", 200.0f }, { "cloud1.spray", 0.45f }, { "cloud1.level", -4.0f },
                                         { "cloud2.level", -10.0f }, { "cloud2.density", 10.0f }, { "res.rain", 0.35f }, { "res.level", -6.0f },
                                         { "weather.wind", 0.1f }, { "weather.rain", 0.1f }, { "weather.surf", 0.25f }, { "medium.drive", 0.3f },
                                         { "tide.rate", 1.2f }, { "drone.sendA", -12.0f } }),
        scene("Deep water", 0.18f, 0.2f, { { "drone.cutoff", 320.0f }, { "drone.density", 5.0f }, { "drone.level", 0.0f }, { "drone.noise", 0.2f },
                                           { "cloud1.density", 6.0f }, { "cloud1.grainMs", 1100.0f }, { "cloud1.spray", 0.2f }, { "cloud1.level", -8.0f },
                                           { "cloud2.level", -60.0f }, { "cloud2.density", 3.0f }, { "res.rain", 0.02f }, { "res.level", -20.0f },
                                           { "weather.wind", 0.0f }, { "weather.rain", 0.0f }, { "weather.surf", 0.45f }, { "medium.drive", 0.35f },
                                           { "tide.rate", 0.4f }, { "drone.sendA", -16.0f } }),
        scene("Storm", 0.84f, 0.2f, { { "drone.cutoff", 900.0f }, { "drone.density", 6.0f }, { "drone.level", -2.0f }, { "drone.noise", 0.55f },
                                      { "cloud1.density", 70.0f }, { "cloud1.grainMs", 90.0f }, { "cloud1.spray", 0.85f }, { "cloud1.level", -6.0f },
                                      { "cloud2.level", -12.0f }, { "cloud2.density", 40.0f }, { "res.rain", 0.55f }, { "res.level", -8.0f },
                                      { "weather.wind", 0.65f }, { "weather.rain", 0.55f }, { "weather.surf", 0.2f }, { "medium.drive", 0.6f },
                                      { "tide.rate", 2.4f }, { "drone.sendA", -8.0f } }),
    };
    s.params["terrain.x"] = 0.5f;
    s.params["terrain.y"] = 0.62f;
    return s;
}

std::string presetPrefix(const std::string& kind)
{
    static const std::map<std::string, std::string> prefixes {
        { "drone", "drone." }, { "cloud", "cloud1." }, { "resonator", "res." }, { "bloom", "bloom." }, { "weather", "weather." },
        { "medium", "medium." }, { "loops", "loops." }, { "looper", "loop." }, { "input", "input." },
    };
    if (const auto it = prefixes.find(kind); it != prefixes.end())
        return it->second;
    return {};
}

void addFactoryPresets(io::PresetLibrary& library)
{
    using V = std::map<std::string, float>;
    auto add = [&](const char* kind, const char* name, V values) { library.addFactory({ name, kind, std::move(values), true }); };
    auto drone = [](float root, float density, float shape, float detune, float cutoff, float res, float noise, float evolve, float driftDepth,
                    float driftRate, float spread, float gravity) {
        return V { { "root", root }, { "density", density }, { "shape", shape }, { "detune", detune }, { "cutoff", cutoff }, { "resonance", res },
                   { "noise", noise }, { "evolve", evolve }, { "driftDepth", driftDepth }, { "driftRate", driftRate }, { "spread", spread }, { "gravity", gravity } };
    };
    add("drone", "Low hum", drone(31, 3, 0.85f, 6, 380, 0.15f, 0.05f, 0.2f, 0.4f, 0.03f, 0.6f, 1.0f));
    add("drone", "Bright organ", drone(43, 5, 0.25f, 4, 3200, 0.1f, 0.02f, 0.35f, 0.3f, 0.05f, 0.8f, 1.0f));
    add("drone", "Breathing reed", drone(38, 4, 0.45f, 12, 1300, 0.45f, 0.4f, 0.5f, 0.6f, 0.08f, 0.7f, 0.8f));
    add("drone", "Wide shimmer", drone(50, 6, 0.3f, 20, 6000, 0.25f, 0.1f, 0.7f, 0.7f, 0.12f, 1.0f, 0.9f));

    auto cloud = [](float density, float grainMs, float position, float spray, float scan, float pitch, float detune, float harmonize, float reverse,
                    float envelope, float stereo, float gravity) {
        return V { { "density", density }, { "grainMs", grainMs }, { "position", position }, { "spray", spray }, { "scan", scan }, { "pitch", pitch },
                   { "pitchSpread", detune }, { "harmonize", harmonize }, { "reverse", reverse }, { "shape", envelope }, { "stereo", stereo },
                   { "gravity", gravity } };
    };
    add("cloud", "Slow smear", cloud(6, 900, 0.3f, 0.2f, 0.05f, 0, 0.05f, 0, 0.1f, 0.7f, 0.8f, 0));
    add("cloud", "Glass dust", cloud(60, 70, 0.5f, 0.8f, 0, 12, 0.2f, 0.3f, 0, 0.2f, 1.0f, 0.5f));
    add("cloud", "Reverse swells", cloud(4, 1600, 0.4f, 0.3f, -0.1f, 0, 0.1f, 0, 1.0f, 0.9f, 0.7f, 0));
    add("cloud", "Octave choir", cloud(18, 350, 0.35f, 0.35f, 0.02f, 0, 0.15f, 1.0f, 0.2f, 0.55f, 0.9f, 1.0f));
    add("cloud", "Frozen point", cloud(30, 220, 0.5f, 0.02f, 0, 0, 0.03f, 0, 0.5f, 0.5f, 0.6f, 0));
    add("cloud", "Endless drone", cloud(8, 1400, 0.5f, 0.9f, 0.02f, 0, 0.02f, 0, 0.3f, 0.85f, 0.7f, 0));
    add("cloud", "Field recording", cloud(12, 600, 0.5f, 1.0f, 0.04f, 0, 0, 0, 0.5f, 0.8f, 1.0f, 0));

    auto res = [](float root, float modes, float structure, float decay, float brightness, float rain, float rainColour, float spread) {
        return V { { "root", root }, { "modes", modes }, { "structure", structure }, { "decay", decay }, { "brightness", brightness },
                   { "rain", rain }, { "rainColour", rainColour }, { "spread", spread }, { "gravity", 1.0f } };
    };
    add("resonator", "Bowed bells", res(62, 16, 0.85f, 12, 0.6f, 0.15f, 0.7f, 0.7f));
    add("resonator", "Strings in rain", res(50, 20, 0.2f, 6, 0.45f, 0.55f, 0.4f, 0.8f));
    add("resonator", "Deep gong", res(38, 12, 1.0f, 30, 0.3f, 0.05f, 0.2f, 0.5f));

    auto bloom = [](float transform, float amount, float length, float attack, float release, float tone, float spread, float random, float position) {
        return V { { "transform", transform }, { "amount", amount }, { "length", length }, { "attack", attack }, { "release", release }, { "pitch", 0.0f },
                   { "tone", tone }, { "spread", spread }, { "random", random }, { "position", position }, { "gravity", 1.0f } };
    };
    add("bloom", "Slow bloom", bloom(0, 0.6f, 12, 0.8f, 5, 0.6f, 0.7f, 0.2f, 0.2f));
    add("bloom", "Smeared glass", bloom(1, 0.7f, 10, 0.05f, 4, 0.8f, 0.8f, 0.4f, 0.3f));
    add("bloom", "Frozen breath", bloom(2, 0.8f, 20, 0.3f, 8, 0.5f, 0.9f, 0.3f, 0.4f));
    add("bloom", "Ghost notes", bloom(3, 0.6f, 8, 0.02f, 3, 0.6f, 0.6f, 0.5f, 0.3f));
    add("bloom", "Constellation", bloom(4, 0.7f, 14, 0.01f, 6, 0.85f, 1.0f, 0.6f, 0.25f));
    add("bloom", "Old tape", bloom(5, 0.6f, 9, 0.05f, 3, 0.4f, 0.5f, 0.3f, 0.3f));
    add("bloom", "Struck halo", bloom(3, 0.8f, 16, 0.01f, 8, 0.7f, 0.8f, 0.3f, 0.05f));
    add("bloom", "Rising voice", bloom(0, 0.8f, 10, 1.5f, 6, 0.55f, 0.7f, 0.2f, 0.6f));
    add("bloom", "Scattered knocks", bloom(4, 0.9f, 12, 0.005f, 5, 0.75f, 1.0f, 0.7f, 0.0f));

    auto weather = [](float wind, float rain, float surf, float gust, float tone, float distance) {
        return V { { "wind", wind }, { "rain", rain }, { "surf", surf }, { "gust", gust }, { "tone", tone }, { "distance", distance } };
    };
    add("weather", "Still air", weather(0, 0, 0, 0.5f, 0.5f, 0.3f));
    add("weather", "Light rain", weather(0.05f, 0.35f, 0, 0.3f, 0.6f, 0.4f));
    add("weather", "Coast", weather(0.25f, 0, 0.6f, 0.5f, 0.45f, 0.5f));
    add("weather", "Distant storm", weather(0.6f, 0.5f, 0.1f, 0.9f, 0.35f, 0.85f));

    auto medium = [](float type, float age, float noise, float wobble, float drive) {
        return V { { "type", type }, { "age", age }, { "noise", noise }, { "wobble", wobble }, { "drive", drive }, { "mix", 1.0f } };
    };
    add("medium", "Clean", medium(0, 0, 0, 0, 0));
    add("medium", "Worn cassette", medium(1, 0.6f, 0.5f, 0.5f, 0.4f));
    add("medium", "Dusty vinyl", medium(2, 0.5f, 0.6f, 0.25f, 0.2f));
    add("medium", "Crunchy sampler", medium(3, 0.5f, 0.4f, 0.1f, 0.6f));

    auto loops = [](float count, float rate, float density, float reg, float spread, float velocity) {
        return V { { "count", count }, { "rate", rate }, { "density", density }, { "register", reg }, { "spread", spread }, { "velocity", velocity } };
    };
    add("loops", "Airports", loops(5, 1, 0.85f, 60, 1.5f, 0.6f));
    add("loops", "Sparse bells", loops(3, 0.5f, 0.6f, 72, 1.0f, 0.5f));
    add("loops", "Busy shore", loops(8, 2, 0.95f, 55, 2.5f, 0.7f));

    auto with = [](V v, const char* key, float value) {
        v[key] = value;
        return v;
    };
    add("cloud", "Low murmur", cloud(10, 800, 0.4f, 0.4f, 0.01f, -12, 0.08f, 0, 0.2f, 0.75f, 0.8f, 0));
    add("cloud", "High shimmer", cloud(35, 120, 0.5f, 0.5f, 0.03f, 12, 0.08f, 0.8f, 0.3f, 0.6f, 1.0f, 1.0f));
    add("cloud", "Slow scan", cloud(10, 500, 0.0f, 0.1f, 0.25f, 0, 0.05f, 0, 0, 0.7f, 0.7f, 0));
    add("cloud", "Rewind", cloud(20, 300, 0.8f, 0.15f, -0.5f, 0, 0.05f, 0, 0.6f, 0.6f, 0.8f, 0));
    add("cloud", "Grain rain", cloud(120, 25, 0.5f, 1.0f, 0, 0, 0.4f, 0, 0.3f, 0.1f, 1.0f, 0));
    add("cloud", "Open fifths", cloud(16, 400, 0.4f, 0.3f, 0.02f, 7, 0.1f, 0.6f, 0.2f, 0.65f, 0.9f, 1.0f));
    add("cloud", "Wide chorus", cloud(24, 260, 0.45f, 0.15f, 0.01f, 0, 0.6f, 0, 0.1f, 0.6f, 1.0f, 0));
    add("cloud", "Sub swell", cloud(5, 1800, 0.3f, 0.2f, 0, -24, 0.02f, 0, 0.2f, 0.95f, 0.5f, 1.0f));
    add("cloud", "Mist", cloud(40, 600, 0.5f, 0.7f, 0, 0, 0.2f, 0.3f, 0.4f, 0.9f, 1.0f, 0.5f));

    add("resonator", "Harp in the wind", res(55, 18, 0.1f, 8, 0.55f, 0.3f, 0.6f, 0.9f));
    add("resonator", "Marimba rain", res(60, 10, 0.55f, 1.2f, 0.5f, 0.6f, 0.5f, 0.8f));
    add("resonator", "Glass rods", res(74, 14, 0.75f, 4, 0.8f, 0.25f, 0.85f, 0.7f));
    add("resonator", "Sympathetic strings", res(50, 24, 0.05f, 15, 0.7f, 0.1f, 0.3f, 0.6f));
    add("resonator", "Dark well", res(31, 8, 0.4f, 20, 0.15f, 0.08f, 0.1f, 0.4f));
    add("resonator", "Tiny bells", res(84, 6, 0.95f, 2, 0.9f, 0.5f, 0.9f, 1.0f));
    add("resonator", "Still pond", res(57, 12, 0.3f, 40, 0.35f, 0.02f, 0.3f, 0.5f));
    add("resonator", "Hail on tin", res(66, 20, 0.9f, 0.6f, 0.75f, 0.9f, 0.95f, 1.0f));

    add("bloom", "Long freeze", bloom(2, 0.9f, 28, 1.0f, 10, 0.45f, 1.0f, 0.15f, 0.5f));
    add("bloom", "Smear and fade", bloom(1, 0.5f, 6, 0.2f, 2.5f, 0.7f, 0.6f, 0.2f, 0.2f));
    add("bloom", "Deep swell", with(bloom(0, 0.7f, 14, 1.2f, 6, 0.4f, 0.8f, 0.1f, 0.3f), "pitch", -12));
    add("bloom", "Night sky", bloom(4, 0.5f, 20, 0.02f, 9, 0.6f, 1.0f, 0.9f, 0.1f));
    add("bloom", "Faded photograph", bloom(5, 0.9f, 12, 0.3f, 5, 0.3f, 0.6f, 0.4f, 0.4f));
    add("bloom", "Echo of a voice", bloom(3, 0.9f, 18, 0.5f, 7, 0.5f, 0.9f, 0.2f, 0.7f));
    add("bloom", "Dry pluck", bloom(0, 0.2f, 2, 0.005f, 0.8f, 0.75f, 0.4f, 0.05f, 0.0f));
    add("bloom", "High halo", with(bloom(2, 0.6f, 16, 0.4f, 7, 0.9f, 1.0f, 0.3f, 0.3f), "pitch", 12));

    auto looper = [](float erosion, float flakes, float overdub) { return V { { "erosion", erosion }, { "flakes", flakes }, { "overdub", overdub } }; };
    add("looper", "Fresh tape", looper(0.05f, 0.0f, 0.95f));
    add("looper", "Gentle wear", looper(0.3f, 0.15f, 0.85f));
    add("looper", "Old reel", looper(0.6f, 0.45f, 0.75f));
    add("looper", "Disintegrating", looper(0.95f, 0.8f, 0.6f));
    add("looper", "Fading echoes", looper(0.4f, 0.1f, 0.45f));
    add("looper", "Dust and dropouts", looper(0.5f, 0.9f, 0.8f));

    add("weather", "Drizzle", weather(0.02f, 0.18f, 0, 0.2f, 0.7f, 0.2f));
    add("weather", "Downpour", weather(0.3f, 0.9f, 0, 0.6f, 0.5f, 0.15f));
    add("weather", "Calm sea", weather(0.05f, 0, 0.35f, 0.2f, 0.4f, 0.6f));
    add("weather", "Gale", weather(0.95f, 0.15f, 0.3f, 1.0f, 0.55f, 0.3f));
    add("weather", "Breeze", weather(0.25f, 0, 0, 0.6f, 0.65f, 0.4f));
    add("weather", "Far shore", weather(0.15f, 0, 0.7f, 0.4f, 0.3f, 0.95f));
    add("weather", "Rain at sea", weather(0.35f, 0.45f, 0.55f, 0.5f, 0.45f, 0.5f));
    add("weather", "Muffled night", weather(0.1f, 0.2f, 0.1f, 0.3f, 0.15f, 0.8f));

    add("medium", "Fresh cassette", medium(1, 0.15f, 0.2f, 0.15f, 0.2f));
    add("medium", "Lost tape", medium(1, 0.95f, 0.7f, 0.85f, 0.5f));
    add("medium", "Clean vinyl", medium(2, 0.15f, 0.25f, 0.1f, 0.1f));
    add("medium", "Attic record", medium(2, 0.9f, 0.85f, 0.4f, 0.3f));
    add("medium", "Lo-fi sampler", medium(3, 0.8f, 0.3f, 0.05f, 0.4f));
    add("medium", "Warm drive", medium(0, 0.1f, 0, 0, 0.7f));
    add("medium", "Seasick", medium(1, 0.5f, 0.3f, 1.0f, 0.25f));
    add("medium", "Faint wear", with(medium(1, 0.4f, 0.3f, 0.3f, 0.2f), "mix", 0.5f));

    add("loops", "Music for rooms", with(loops(4, 0.6f, 0.7f, 64, 1.0f, 0.45f), "pattern", 3));
    add("loops", "Low tides", with(loops(3, 0.4f, 0.75f, 43, 1.0f, 0.6f), "pattern", 7));
    add("loops", "Little machine", with(loops(6, 1.5f, 0.8f, 76, 1.0f, 0.4f), "pattern", 11));
    add("loops", "Wide sky", with(loops(7, 0.8f, 0.7f, 60, 3.0f, 0.5f), "pattern", 21));
    add("loops", "One voice", with(loops(1, 0.5f, 0.9f, 62, 0.5f, 0.55f), "pattern", 0));
    add("loops", "Rain of notes", with(loops(8, 3.0f, 0.6f, 70, 2.0f, 0.35f), "pattern", 42));
    add("loops", "Slow canon", with(loops(5, 0.3f, 0.95f, 55, 1.5f, 0.6f), "pattern", 15));

    auto input = [](float gain, float lowCut, float gate, float freezeLevel, float freezeDrift) {
        return V { { "gain", gain }, { "highPass", lowCut }, { "gate", gate }, { "freezeLevel", freezeLevel }, { "freezeDrift", freezeDrift } };
    };
    add("input", "Voice", input(6, 90, -60, -3, 0.3f));
    add("input", "Guitar", input(0, 70, -65, 0, 0.35f));
    add("input", "Field microphone", input(12, 120, -80, -6, 0.6f));
    add("input", "Synth line", input(-6, 30, -75, 0, 0.2f));
    add("input", "Quiet room", input(18, 60, -50, -6, 0.5f));
    add("input", "Cello", input(3, 40, -62, -2, 0.25f));
}
}

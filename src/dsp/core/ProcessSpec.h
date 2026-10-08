#pragma once

namespace tf::dsp {
struct ProcessSpec
{
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
};
}

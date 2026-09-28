#pragma once

#include <vector>

#include "oklab.h"

enum Channel { Contrast, Colour, Chroma, Centre, ChannelCount };

struct FixationPoint {
    double x = 0.5;
    double y = 0.5;
};

const char* ChannelName(Channel c);

Plane ContrastChannel(const PreparedImage& img);
Plane ColourChannel(const PreparedImage& img);
Plane ChromaChannel(const PreparedImage& img);
Plane CentreChannel(int rows, int cols);

Plane Stretch(const Plane& raw);
double TopLevel(const Plane& total);
std::vector<char> TopRegion(const Plane& total);
FixationPoint FindFixation(const Plane& total);

// The channel maps are built once per image; changing a weight only recombines them.
class SaliencyModel {
public:
    SaliencyModel();

    void SetImage(const PreparedImage& img);
    void SetWeight(Channel c, double w);

    double Weight(Channel c) const { return weights_[c]; }
    const Plane& ChannelMap(Channel c) const { return channels_[c]; }
    const Plane& Total() const { return total_; }
    const FixationPoint& Fixation() const { return fixation_; }
    double Share(Channel c) const { return shares_[c]; }
    double TopShare(Channel c) const { return topShares_[c]; }

private:
    void Combine();

    double weights_[ChannelCount];
    Plane channels_[ChannelCount];
    Plane total_;
    FixationPoint fixation_;
    double shares_[ChannelCount] = {};
    double topShares_[ChannelCount] = {};
};

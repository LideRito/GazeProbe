#include "saliency.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "filters.h"

static const char* const CHANNEL_NAMES[ChannelCount] = {"contrast", "colour", "chroma", "face", "centre"};

// A guess rather than a fit: nobody has eye-tracked these pictures.
static const double DEFAULT_WEIGHTS[ChannelCount] = {1.0, 0.7, 0.4, 1.2, 0.3};

static const double SURROUND = 0.06;
static const double CENTRE_SIGMA = 0.28;
static const double TOP_FRACTION = 0.1;

// A channel narrower than this is flat; it has to sit above LocalStd's ~1e-8 residue.
static const double FLAT = 1e-4;

// The top clip is 99.9 and not 98 because a face or an accent can be under 2% of the canvas.
static const double CLIP_LOW = 2.0;
static const double CLIP_HIGH = 99.9;

// Evenly spaced from 0 to 1 inclusive, with the last sample exactly 1.
static double UnitCoord(int i, int n) {
    if (n == 1) return 0.0;
    if (i == n - 1) return 1.0;
    return i * (1.0 / (n - 1));
}

// Linear interpolation between order statistics, taken from the nearer one so both ends stay exact.
static double Percentile(const std::vector<double>& sorted, double q) {
    const std::size_t n = sorted.size();
    const double position = double(n - 1) * (q / 100.0);
    const double below = std::floor(position);
    const std::size_t i0 = std::min(std::size_t(below), n - 1);
    const std::size_t i1 = std::min(i0 + 1, n - 1);
    const double t = position - below;
    const double diff = sorted[i1] - sorted[i0];
    return t >= 0.5 ? sorted[i1] - diff * (1.0 - t) : sorted[i0] + diff * t;
}

static bool MinMax(const Plane& p, double& lo, double& hi) {
    if (p.Empty()) return false;
    const auto [low, high] = std::minmax_element(p.v.begin(), p.v.end());
    lo = *low;
    hi = *high;
    return true;
}

static void Normalise(Plane& p) {
    double lo = 0.0, hi = 0.0;
    if (!MinMax(p, lo, hi) || hi - lo < 1e-12) {
        std::fill(p.v.begin(), p.v.end(), 0.0);
        return;
    }
    for (double& x : p.v) x = (x - lo) / (hi - lo);
}

// 4-connected components, numbered from 1 in the raster order of their first pixel.
static int LabelComponents(const std::vector<char>& mask, int rows, int cols,
                           std::vector<int>& labels) {
    labels.assign(mask.size(), 0);
    std::vector<int> stack;
    int count = 0;
    for (int start = 0; start < rows * cols; ++start) {
        if (!mask[start] || labels[start]) continue;
        ++count;
        labels[start] = count;
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int r = i / cols, c = i % cols;
            const int around[4] = {r > 0 ? i - cols : -1, r < rows - 1 ? i + cols : -1,
                                   c > 0 ? i - 1 : -1, c < cols - 1 ? i + 1 : -1};
            for (int j : around) {
                if (j >= 0 && mask[j] && !labels[j]) {
                    labels[j] = count;
                    stack.push_back(j);
                }
            }
        }
    }
    return count;
}

static void ToShares(const double* totals, double* out) {
    double grand = 0.0;
    for (int c = 0; c < ChannelCount; ++c) grand += totals[c];
    for (int c = 0; c < ChannelCount; ++c) out[c] = grand > 0.0 ? totals[c] / grand : 0.0;
}

const char* ChannelName(Channel c) {
    return CHANNEL_NAMES[c];
}

Plane ContrastChannel(const PreparedImage& img) {
    return LocalStd(img.L, WindowSize(img.Rows(), img.Cols()));
}

// How far each pixel's colour sits from the average colour of its surroundings.
Plane ColourChannel(const PreparedImage& img) {
    const double sigma = SURROUND * std::max(img.Rows(), img.Cols());
    const Plane aroundA = Blur(img.a, sigma);
    const Plane aroundB = Blur(img.b, sigma);
    Plane out(img.Rows(), img.Cols());
    for (std::size_t i = 0; i < out.Size(); ++i)
        out.v[i] = std::hypot(img.a.v[i] - aroundA.v[i], img.b.v[i] - aroundB.v[i]);
    return out;
}

Plane ChromaChannel(const PreparedImage& img) {
    Plane out(img.Rows(), img.Cols());
    for (std::size_t i = 0; i < out.Size(); ++i) out.v[i] = std::hypot(img.a.v[i], img.b.v[i]);
    return out;
}

// A Gaussian as wide as each box; where faces overlap the larger value wins, so the channel stays in [0, 1].
Plane FaceChannel(const std::vector<FaceBox>& faces, int rows, int cols) {
    Plane out(rows, cols);
    for (const FaceBox& f : faces) {
        const double sx = std::max(f.w, 1) / 2.0, sy = std::max(f.h, 1) / 2.0;
        const double cx = f.x + sx, cy = f.y + sy;
        for (int r = 0; r < rows; ++r) {
            const double dy = (r - cy) / sy;
            for (int c = 0; c < cols; ++c) {
                const double dx = (c - cx) / sx;
                out.At(r, c) = std::max(out.At(r, c), std::exp(-0.5 * (dx * dx + dy * dy)));
            }
        }
    }
    return out;
}

// Viewers look at the middle of a canvas first, whatever is in it.
Plane CentreChannel(int rows, int cols) {
    Plane out(rows, cols);
    const double spread = 2.0 * (CENTRE_SIGMA * CENTRE_SIGMA);
    for (int r = 0; r < rows; ++r) {
        const double dy = UnitCoord(r, rows) - 0.5;
        for (int c = 0; c < cols; ++c) {
            const double dx = UnitCoord(c, cols) - 0.5;
            out.At(r, c) = std::exp(-(dx * dx + dy * dy) / spread);
        }
    }
    return out;
}

Plane Stretch(const Plane& raw) {
    Plane out(raw.rows, raw.cols);
    std::vector<double> values = raw.v;
    if (values.empty()) return out;
    std::sort(values.begin(), values.end());

    double lo = Percentile(values, CLIP_LOW);
    double hi = Percentile(values, CLIP_HIGH);
    if (hi - lo < FLAT) {
        lo = values.front();
        hi = values.back();
    }
    if (hi - lo < FLAT) return out;
    for (std::size_t i = 0; i < out.Size(); ++i)
        out.v[i] = std::clamp((raw.v[i] - lo) / (hi - lo), 0.0, 1.0);
    return out;
}

double TopLevel(const Plane& total) {
    double lo = 0.0, hi = 0.0;
    MinMax(total, lo, hi);
    return hi - TOP_FRACTION * (hi - lo);
}

std::vector<char> TopRegion(const Plane& total) {
    std::vector<char> top(total.Size(), 0);
    double lo = 0.0, hi = 0.0;
    if (!MinMax(total, lo, hi) || hi <= 0.0) return top;
    const double level = TopLevel(total);
    for (std::size_t i = 0; i < total.Size(); ++i) top[i] = total.v[i] >= level;
    return top;
}

FixationPoint FindFixation(const Plane& total, const std::vector<char>& top) {
    FixationPoint f;
    if (std::find(top.begin(), top.end(), char(1)) == top.end()) return f;

    // One peak, not the average of all of them: two faces would centroid into the gap between.
    std::vector<int> labels;
    const int count = LabelComponents(top, total.rows, total.cols, labels);
    int keep = 0;
    if (count > 1) {
        std::vector<double> mass(count + 1, 0.0);
        for (std::size_t i = 0; i < labels.size(); ++i)
            if (labels[i]) mass[labels[i]] += total.v[i];
        keep = int(std::max_element(mass.begin() + 1, mass.end()) - mass.begin());
    }

    double sum = 0.0, sumX = 0.0, sumY = 0.0;
    for (std::size_t i = 0; i < top.size(); ++i) {
        if (!top[i] || (keep && labels[i] != keep)) continue;
        const double w = total.v[i];
        sum += w;
        sumX += double(i % std::size_t(total.cols)) * w;
        sumY += double(i / std::size_t(total.cols)) * w;
    }
    f.x = sumX / sum / total.cols;
    f.y = sumY / sum / total.rows;
    return f;
}

SaliencyModel::SaliencyModel() {
    for (int c = 0; c < ChannelCount; ++c) weights_[c] = DEFAULT_WEIGHTS[c];
}

void SaliencyModel::SetImage(const PreparedImage& img, const std::vector<FaceBox>& faces) {
    channels_[Contrast] = Stretch(ContrastChannel(img));
    channels_[Colour] = Stretch(ColourChannel(img));
    channels_[Chroma] = Stretch(ChromaChannel(img));
    channels_[Face] = FaceChannel(faces, img.Rows(), img.Cols());
    channels_[Centre] = Stretch(CentreChannel(img.Rows(), img.Cols()));
    Combine();
}

void SaliencyModel::SetWeight(Channel c, double w) {
    weights_[c] = w;
    if (!channels_[0].Empty()) Combine();
}

void SaliencyModel::Combine() {
    total_ = Plane(channels_[0].rows, channels_[0].cols);
    for (int c = 0; c < ChannelCount; ++c) {
        const double w = std::max(0.0, weights_[c]);
        for (std::size_t i = 0; i < total_.Size(); ++i) total_.v[i] += w * channels_[c].v[i];
    }
    Normalise(total_);
    top_ = TopRegion(total_);
    fixation_ = FindFixation(total_, top_);

    // The sum is linear, so each channel's part of it can be read off exactly.
    double whole[ChannelCount] = {}, inTop[ChannelCount] = {};
    for (int c = 0; c < ChannelCount; ++c) {
        const double w = std::max(0.0, weights_[c]);
        for (std::size_t i = 0; i < total_.Size(); ++i) {
            const double part = w * channels_[c].v[i];
            whole[c] += part;
            if (top_[i]) inTop[c] += part;
        }
    }
    ToShares(whole, shares_);
    ToShares(inTop, topShares_);
}

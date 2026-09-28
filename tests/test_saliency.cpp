#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "saliency.h"

static PreparedImage Grey(int rows, int cols, double L) {
    return PreparedImage{Plane(rows, cols, L), Plane(rows, cols, 0.0), Plane(rows, cols, 0.0)};
}

static void Disc(PreparedImage& img, int cr, int cc, int radius, double L, double a, double b) {
    for (int r = cr - radius; r <= cr + radius; ++r) {
        for (int c = cc - radius; c <= cc + radius; ++c) {
            if ((r - cr) * (r - cr) + (c - cc) * (c - cc) > radius * radius) continue;
            img.L.At(r, c) = L;
            img.a.At(r, c) = a;
            img.b.At(r, c) = b;
        }
    }
}

static double Largest(const Plane& p) {
    double m = 0.0;
    for (double x : p.v) m = std::fmax(m, std::fabs(x));
    return m;
}

// Distance in pixels from the fixation to (row, col).
static double PixelsFrom(const FixationPoint& f, int rows, int cols, double row, double col) {
    return std::hypot(f.x * cols - col, f.y * rows - row);
}

int main() {
    Checker ck;
    std::printf("test_saliency\n");

    ck.Require("channels are named in order",
               !std::strcmp(ChannelName(Contrast), "contrast") &&
                   !std::strcmp(ChannelName(Colour), "colour") &&
                   !std::strcmp(ChannelName(Chroma), "chroma") &&
                   !std::strcmp(ChannelName(Centre), "centre"));

    SaliencyModel flatModel;
    flatModel.SetImage(Grey(81, 121, 0.5));
    ck.Close("a flat picture has no contrast", Largest(flatModel.ChannelMap(Contrast)), 0.0, 0.0);
    ck.Close("no colour contrast", Largest(flatModel.ChannelMap(Colour)), 0.0, 0.0);
    ck.Close("no chroma", Largest(flatModel.ChannelMap(Chroma)), 0.0, 0.0);
    ck.Close("only the centre is left", Largest(flatModel.ChannelMap(Centre)), 1.0, 0.0);
    ck.Require("so the eye lands in the middle",
               std::fabs(flatModel.Fixation().x - 0.5) <= 1.0 / 121 &&
                   std::fabs(flatModel.Fixation().y - 0.5) <= 1.0 / 81);

    PreparedImage one = Grey(120, 160, 0.5);
    Disc(one, 30, 40, 6, 0.9, 0.12, 0.06);
    SaliencyModel model;
    model.SetImage(one);
    // Contrast is an edge measure, so the eye lands on the dot's rim rather than its middle.
    ck.Require("a bright saturated dot draws the eye onto it",
               PixelsFrom(model.Fixation(), 120, 160, 30, 40) <= 6.0);

    double shares = 0.0, topShares = 0.0;
    for (int c = 0; c < ChannelCount; ++c) {
        shares += model.Share(Channel(c));
        topShares += model.TopShare(Channel(c));
    }
    ck.Close("shares add up to the whole", shares, 1.0, 1e-12);
    ck.Close("and so do the shares in the top region", topShares, 1.0, 1e-12);

    // Two equal peaks: the answer is one of them, never the empty gap between.
    PreparedImage two = Grey(100, 200, 0.5);
    Disc(two, 50, 40, 6, 0.9, 0.12, 0.06);
    Disc(two, 50, 160, 6, 0.9, 0.12, 0.06);
    SaliencyModel pair;
    pair.SetImage(two);
    const double nearOne = std::fmin(PixelsFrom(pair.Fixation(), 100, 200, 50, 40),
                                      PixelsFrom(pair.Fixation(), 100, 200, 50, 160));
    ck.Require("two peaks: it lands on one of them", nearOne <= 6.0);
    ck.Require("and not in the gap between them", PixelsFrom(pair.Fixation(), 100, 200, 50, 100) > 40.0);

    // A gently varying background with an accent covering 1% of the canvas.
    PreparedImage accent = Grey(100, 100, 0.5);
    for (int r = 0; r < 100; ++r)
        for (int c = 0; c < 100; ++c) accent.a.At(r, c) = 0.05 * c / 99.0;
    for (int r = 45; r < 55; ++r)
        for (int c = 45; c < 55; ++c) accent.a.At(r, c) = 0.2;
    const Plane stretched = Stretch(ChromaChannel(accent));
    ck.Close("a small accent reaches the top", stretched.At(50, 50), 1.0, 0.0);
    ck.Require("and stays apart from the background", stretched.At(10, 99) < 0.5);

    Plane dust(50, 50);
    for (std::size_t i = 0; i < dust.Size(); ++i) dust.v[i] = 0.5 + 1e-6 * double((i * 7919) % 13) / 13.0;
    ck.Close("rounding noise is not stretched into a signal", Largest(Stretch(dust)), 0.0, 0.0);
    ck.Close("a constant stretches to zero", Largest(Stretch(Plane(10, 10, 0.3))), 0.0, 0.0);

    Plane levels(1, 3);
    levels.At(0, 1) = 0.5;
    levels.At(0, 2) = 1.0;
    ck.Close("the top tenth starts at 0.9", TopLevel(levels), 0.9, 1e-15);
    const std::vector<char> top = TopRegion(levels);
    ck.Require("and only the highest sample is in it", !top[0] && !top[1] && top[2]);
    const std::vector<char> blank = TopRegion(Plane(4, 4));
    ck.Require("a blank total has no top region", std::find(blank.begin(), blank.end(), 1) == blank.end());

    const Plane colourBefore = model.ChannelMap(Colour);
    const Plane totalBefore = model.Total();
    model.SetWeight(Colour, 2.0);
    ck.Close("a new weight leaves the channels alone", model.ChannelMap(Colour), colourBefore, 0.0);
    double moved = 0.0;
    for (std::size_t i = 0; i < totalBefore.Size(); ++i)
        moved = std::fmax(moved, std::fabs(model.Total().v[i] - totalBefore.v[i]));
    ck.Require("but changes the total", moved > 0.0);

    model.SetWeight(Chroma, -1.0);
    ck.Close("a negative weight counts as zero", model.Share(Chroma), 0.0, 0.0);

    for (int c = 0; c < ChannelCount; ++c) model.SetWeight(Channel(c), 0.0);
    ck.Close("all weights off: nothing to look at", Largest(model.Total()), 0.0, 0.0);
    ck.Require("and the eye rests in the middle", model.Fixation().x == 0.5 && model.Fixation().y == 0.5);

    return ck.Report("test_saliency");
}

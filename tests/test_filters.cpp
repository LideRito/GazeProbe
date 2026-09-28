#include <cmath>
#include <cstdio>

#include "check.h"
#include "filters.h"

static double MaxAbsDiff(const Plane& p, double value) {
    double worst = 0.0;
    for (double x : p.v) worst = std::fmax(worst, std::fabs(x - value));
    return worst;
}

int main() {
    Checker ck;
    std::printf("test_filters\n");

    ck.Close("window for a 1024 edge", WindowSize(1024, 715), 21, 0.0);
    ck.Close("window never below 3", WindowSize(40, 30), 3, 0.0);
    ck.Close("an even window is made odd", WindowSize(500, 100), 11, 0.0);

    // Reflect repeats the edge sample, so a 3-wide box at the start of 0,1,2... averages 0,0,1.
    Plane ramp(1, 8);
    for (int c = 0; c < 8; ++c) ramp.At(0, c) = c;
    const Plane boxed = BoxFilter(ramp, 3, Border::Reflect);
    ck.Close("reflect at the first sample", boxed.At(0, 0), 1.0 / 3.0, 1e-15);
    ck.Close("plain mean inside", boxed.At(0, 3), 3.0, 1e-15);
    ck.Close("reflect at the last sample", boxed.At(0, 7), 20.0 / 3.0, 1e-15);

    Plane impulse(1, 9);
    impulse.At(0, 4) = 1.0;
    const Plane even = BoxFilter(impulse, 4, Border::Nearest);
    ck.Require("an even box covers i-2 .. i+1",
               even.At(0, 2) == 0.0 && even.At(0, 3) == 0.25 && even.At(0, 6) == 0.25 &&
                   even.At(0, 7) == 0.0);

    const Plane flat(20, 30, 0.37);
    ck.Close("box leaves a flat field alone", MaxAbsDiff(BoxFilter(flat, 5, Border::Reflect), 0.37),
             0.0, 1e-15);
    ck.Close("gaussian leaves it alone", MaxAbsDiff(GaussianFilter(flat, 2.5, Border::Nearest), 0.37),
             0.0, 1e-15);
    ck.Close("wide blur leaves it alone", MaxAbsDiff(Blur(flat, 40.0), 0.37), 0.0, 1e-15);

    Plane dot(41, 41);
    dot.At(20, 20) = 1.0;
    const Plane g = GaussianFilter(dot, 3.0, Border::Nearest);
    double mass = 0.0;
    for (double x : g.v) mass += x;
    ck.Close("gaussian keeps an impulse's mass", mass, 1.0, 1e-14);
    ck.Require("gaussian is symmetric", g.At(20, 15) == g.At(20, 25) && g.At(15, 20) == g.At(25, 20));
    ck.Require("its radius is 4 sigma", g.At(20, 32) > 0.0 && g.At(20, 33) == 0.0);

    // Linear interpolation is exact on a linear ramp, in both directions.
    Plane line(1, 4);
    for (int c = 0; c < 4; ++c) line.At(0, c) = c;
    const Plane wide = ResizeLinear(line, 1, 8);
    double rampWorst = 0.0;
    for (int c = 0; c < 8; ++c) rampWorst = std::fmax(rampWorst, std::fabs(wide.At(0, c) - c * 3.0 / 7.0));
    ck.Close("upsampling a ramp stays linear", rampWorst, 0.0, 1e-15);
    ck.Close("the last sample lines up", wide.At(0, 7), 3.0, 0.0);
    const Plane back = ResizeLinear(wide, 1, 4);
    ck.Close("down again recovers it", back.At(0, 2), 2.0, 1e-15);

    Plane step(12, 80);
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 80; ++c) step.At(r, c) = c < 40 ? 0.0 : 1.0;
    const Plane soft = Blur(step, 20.0);
    bool monotone = true;
    for (int c = 1; c < 80; ++c) monotone = monotone && soft.At(6, c) >= soft.At(6, c - 1) - 1e-15;
    ck.Require("a wide blur of a step rises monotonically", monotone);
    ck.Require("and stays inside the step's range", soft.At(6, 0) >= 0.0 && soft.At(6, 79) <= 1.0);
    ck.Close("a narrow blur is a plain gaussian", Blur(step, 3.0),
             GaussianFilter(step, 3.0, Border::Nearest), 0.0);

    ck.Close("local std of a flat field is near zero", MaxAbsDiff(LocalStd(flat, 5), 0.0), 0.0, 1e-7);

    // A 5-wide window across a 0.2 / 0.8 edge sees three of one and two of the other.
    Plane edge(30, 60);
    for (int r = 0; r < 30; ++r)
        for (int c = 0; c < 60; ++c) edge.At(r, c) = c < 30 ? 0.2 : 0.8;
    const Plane spread = LocalStd(edge, 5);
    ck.Close("std at the edge", spread.At(15, 29), std::sqrt(0.0864), 1e-12);
    ck.Close("zero away from it", spread.At(15, 5), 0.0, 1e-7);
    ck.Close("symmetric about it", spread.At(15, 30), spread.At(15, 29), 1e-15);

    return ck.Report("test_filters");
}

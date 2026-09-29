#include <cmath>
#include <cstdio>

#include "check.h"
#include "oklab.h"

int main() {
    Checker ck;
    std::printf("test_oklab\n");

    ck.Close("srgb 0 is linear 0", SrgbToLinear(0.0), 0.0, 0.0);
    ck.Close("srgb 1 is linear 1", SrgbToLinear(1.0), 1.0, 1e-15);
    ck.Close("srgb 0.5 is linear 0.214", SrgbToLinear(0.5), 0.21404114048223255, 1e-15);

    // The curve is piecewise, so the two halves have to meet at the 0.04045 join.
    ck.Close("the two halves meet", SrgbToLinear(0.04045 - 1e-12),
             SrgbToLinear(0.04045 + 1e-12), 1e-8);

    // sRGB red, which CSS Color 4 lists as oklch(62.8% 0.2577 29.23).
    double redL, redA, redB;
    SrgbToOklab(1.0, 0.0, 0.0, redL, redA, redB);
    ck.Close("red lightness", redL, 0.628, 5e-4);
    ck.Close("red chroma", std::hypot(redA, redB), 0.2577, 5e-5);
    ck.Close("red hue in degrees", std::atan2(redB, redA) * 180.0 / 3.14159265358979323846, 29.23, 5e-3);

    // Not zero: the published LMS rows do not sum to exactly 1, leaving ~3.7e-8 at white.
    double greyWorst = 0.0;
    for (int i = 0; i <= 32; ++i) {
        double L, a, b;
        SrgbToOklab(i / 32.0, i / 32.0, i / 32.0, L, a, b);
        greyWorst = std::fmax(greyWorst, std::fmax(std::fabs(a), std::fabs(b)));
    }
    ck.Close("grey stays achromatic", greyWorst, 0.0, 1e-6);

    double whiteL, blackL, ignoredA, ignoredB;
    SrgbToOklab(1.0, 1.0, 1.0, whiteL, ignoredA, ignoredB);
    SrgbToOklab(0.0, 0.0, 0.0, blackL, ignoredA, ignoredB);
    ck.Close("white has L = 1", whiteL, 1.0, 1e-6);
    ck.Close("black has L = 0", blackL, 0.0, 1e-9);

    // Lightness has to be monotonic in grey level or every value measure is meaningless.
    bool rising = true;
    double previous = -1.0;
    for (int i = 0; i <= 64; ++i) {
        double L, a, b;
        SrgbToOklab(i / 64.0, i / 64.0, i / 64.0, L, a, b);
        rising = rising && L > previous;
        previous = L;
    }
    ck.Require("lightness rises with grey level", rising);

    double L, a, b;
    SrgbToOklab(0.0, 0.0, 1.2, L, a, b);
    ck.Require("out-of-gamut input stays finite",
               std::isfinite(L) && std::isfinite(a) && std::isfinite(b));

    const int n = 8;
    Plane r(n, n, 0.8), g(n, n, 0.3), bp(n, n, 0.2);
    const PreparedImage flat = ToOklab(r, g, bp);
    double spread = 0.0;
    for (std::size_t i = 0; i < flat.L.Size(); ++i)
        spread = std::fmax(spread, std::fabs(flat.L.v[i] - flat.L.v[0]));
    ck.Close("a flat patch stays flat", spread, 0.0, 0.0);

    return ck.Report("test_oklab");
}

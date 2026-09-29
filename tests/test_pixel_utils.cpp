#include <cstdio>

#include "check.h"
#include "pixel_utils.h"

int main() {
    Checker ck;
    std::printf("test_pixel_utils\n");

    ck.Require("halves round to even",
               RoundHalfEven(2.5) == 2 && RoundHalfEven(3.5) == 4 && RoundHalfEven(-0.5) == 0);
    ck.Require("everything else to the nearest", RoundHalfEven(2.4) == 2 && RoundHalfEven(2.6) == 3);

    ck.Require("levels span 0 to 255", ToLevel(0.0) == 0 && ToLevel(1.0) == 255);
    ck.Require("a half is level 128", ToLevel(0.5) == 128);
    ck.Require("out of range is clamped", ToLevel(-0.2) == 0 && ToLevel(1.3) == 255);

    ck.Require("a long edge scales exactly", ScaledSize(1024, 0.625) == 640);
    ck.Require("a scaled edge keeps at least one pixel", ScaledSize(3, 0.1) == 1);

    return ck.Report("test_pixel_utils");
}

#include "pixel_utils.h"

#include <algorithm>
#include <cmath>

// nearbyint under the default rounding mode sends halves to the even neighbour: 2.5 to 2, 3.5 to 4.
int RoundHalfEven(double x) {
    return int(std::nearbyint(x));
}

// A value in [0, 1] as an 8-bit level from 0 to 255.
int ToLevel(double x) {
    return RoundHalfEven(std::clamp(x, 0.0, 1.0) * 255.0);
}

int ScaledSize(int n, double scale) {
    return std::max(1, RoundHalfEven(n * scale));
}

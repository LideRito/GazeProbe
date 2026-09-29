#include "overlay_utils.h"

#include <algorithm>

// Five stops of the inferno colour map, linearly interpolated.
static void HeatColour(double t, double& r, double& g, double& b) {
    static const double INFERNO[5][3] = {
        {0.0015, 0.0005, 0.0139},
        {0.3411, 0.0623, 0.4290},
        {0.7355, 0.2155, 0.3303},
        {0.9779, 0.5570, 0.0350},
        {0.9884, 0.9984, 0.6449},
    };
    const double x = std::clamp(t, 0.0, 1.0) * 4.0;
    const int i = std::min(int(x), 3);
    const double f = x - i;
    r = INFERNO[i][0] + (INFERNO[i + 1][0] - INFERNO[i][0]) * f;
    g = INFERNO[i][1] + (INFERNO[i + 1][1] - INFERNO[i][1]) * f;
    b = INFERNO[i][2] + (INFERNO[i + 1][2] - INFERNO[i][2]) * f;
}

RgbImage HeatImage(const Plane& total) {
    RgbImage out{Plane(total.rows, total.cols), Plane(total.rows, total.cols), Plane(total.rows, total.cols)};
    for (std::size_t i = 0; i < total.Size(); ++i) HeatColour(total.v[i], out.r.v[i], out.g.v[i], out.b.v[i]);
    return out;
}

RgbImage GreyImage(const Plane& p) {
    return RgbImage{p, p, p};
}

// A region pixel is on the edge when it touches the canvas border or a pixel outside the region.
std::vector<char> RegionEdge(const std::vector<char>& region, int rows, int cols) {
    std::vector<char> edge(region.size(), 0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::size_t i = std::size_t(r) * cols + c;
            if (!region[i]) continue;
            edge[i] = r == 0 || c == 0 || r == rows - 1 || c == cols - 1 || !region[i - cols] ||
                      !region[i + cols] || !region[i - 1] || !region[i + 1];
        }
    }
    return edge;
}

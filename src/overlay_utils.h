#pragma once

#include <vector>

#include "image_io.h"

// How the result is drawn over the picture, shared so the saved images and the viewer look the same.
const double HEAT_OPACITY = 0.55;
const double MARK[3] = {0.0, 1.0, 0.53};
const double FACE_MARK[3] = {0.3, 0.8, 1.0};

RgbImage HeatImage(const Plane& total);
RgbImage GreyImage(const Plane& p);
std::vector<char> RegionEdge(const std::vector<char>& region, int rows, int cols);

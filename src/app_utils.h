#pragma once

#include <chrono>
#include <string>

// Pictures are analysed with their long edge at this size, whatever size they arrive in.
const int LONG_EDGE = 1024;

class Stopwatch {
public:
    double LapMs();

private:
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

std::wstring DefaultModelPath();

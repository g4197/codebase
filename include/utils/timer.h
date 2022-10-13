#ifndef TIMER_H_
#define TIMER_H_

#include <time.h>

#include <cstdint>
#include <string>

/*
 * A simple timer using CLOCK_REALTIME
 */

class Timer {
public:
    enum { ns = 0, us = 1, ms = 2, s = 3 };
    static constexpr char unit_str[4][4] = {
        "ns",
        "us",
        "ms",
        "s",
    };

    Timer &begin() {
        clock_gettime(CLOCK_REALTIME, &start_);
        return *this;
    }

    Timer &end() {
        clock_gettime(CLOCK_REALTIME, &end_);
        return *this;
    }

    double elapsed(int unit = us) {
        double elapsed = (end_.tv_sec - start_.tv_sec) * 1e9 + (end_.tv_nsec - start_.tv_nsec);
        switch (unit) {
            case ns:
                return elapsed;
            case us:
                return elapsed / 1000;
            case ms:
                return elapsed / 1000000;
            case s:
                return elapsed / 1000000000;
            default:
                return elapsed;
        }
    }

    std::string elapsed_str(int unit = us) {
        return std::to_string(elapsed(unit)) + unit_str[unit];
    }

private:
    timespec start_, end_;
};

#endif  // TIMER_H_
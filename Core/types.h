//
// Created by ricar on 10/04/2026.
//

#pragma once

#include "Core/Dates.h"
#include <algorithm>


namespace Core {

    struct Point {
        double time;
        double value;
    };

    enum class DayCount { ACT_360, ACT_365, DC_30_360 };

    inline double year_fraction(const Date& d1, const Date& d2, const DayCount dc) {
        switch (dc) {
            case DayCount::ACT_360: return static_cast<double>(d2 - d1) / 360.0;
            case DayCount::ACT_365: return static_cast<double>(d2 - d1) / 365.0;
            case DayCount::DC_30_360: {
                auto [y1, m1, dd1] = Date::fromJulian(d1.getJulianDays());
                auto [y2, m2, dd2] = Date::fromJulian(d2.getJulianDays());
                dd1 = std::min(dd1, 30);
                if (dd1 == 30) dd2 = std::min(dd2, 30);
                return ((y2-y1)*360.0 + (m2-m1)*30.0 + (dd2-dd1)) / 360.0;
            }
        }
        return static_cast<double>(d2 - d1) / 365.0;
    }

    enum class Frequency {
        QUARTERLY,
        SEMI_ANNUAL,
        ANNUAL,
    };

}




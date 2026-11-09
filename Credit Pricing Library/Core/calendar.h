
#pragma once

#include "Dates.h"

namespace Core {

/// @brief Adjustement convention for dates
enum class BusinessDayConvention {
    Following,         // If holidays, we take the next day
    ModifiedFollowing, // Following convention, but unless we change month
    Preceding,         // If holiday, we take the previous business day
    ModifiedPreceding, // Preceding, unless we change month
    Unadjusted         // None modification
};

/// @brief Easter sunday for the TARGET calendar
[[nodiscard]] constexpr Date easter_sunday(int year) noexcept {
    const int a = year % 19, b = year / 100, c = year % 100;
    const int d = b / 4, e = b % 4;
    const int f = (b + 8) / 25, g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4, k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int n = h + l - 7 * m + 114;
    return Date::from_ymd_unchecked(year, n / 31, n % 31 + 1);
}

/// @brief Static interface for instantiation of calendars, while respecting the open closed principle.
/// In order to avoid overhead since it may be used into loops (polymorphism isn't a good idea).
template <typename Derived>
struct CalendarBase {
/// @brief Static virtual function to check business days based on business convention
    [[nodiscard]] constexpr bool is_business_day(Date d) const noexcept {
        return static_cast<const Derived*>(this)->is_business_day_impl(d);
    }
/// @brief Ajust a date according to the convention following
    [[nodiscard]] constexpr Date adjust(Date d) const noexcept {
        return adjust_following(d);
    }
/// @brief Adjust a date according to a given business convention as parameter
    [[nodiscard]] constexpr Date adjust(Date d, BusinessDayConvention convention) const noexcept {
        switch (convention) {
            case BusinessDayConvention::Following:
                return adjust_following(d);
            case BusinessDayConvention::ModifiedFollowing:
                return adjust_modified_following(d);
            case BusinessDayConvention::Preceding:
                return adjust_preceding(d);
            case BusinessDayConvention::ModifiedPreceding:
                return adjust_modified_preceding(d);
            case BusinessDayConvention::Unadjusted:
                return d;
        }
        return adjust_following(d);
    }
    /// @brief Function to add a fixed number of business days to a date
    [[nodiscard]] constexpr Date advance(Date d, int n_business_days) const noexcept {
        Date result = d;
        const int step = (n_business_days >= 0) ? 1 : -1;
        int remaining = (n_business_days >= 0) ? n_business_days : -n_business_days;

        while (remaining > 0) {
            result += step;
            if (is_business_day(result)) {
                --remaining;
            }
        }
        return result;
    }
    /// @brief Count the number of business days between two dates, in [d1, d2[
    [[nodiscard]] constexpr int business_days_between(Date d1, Date d2) const noexcept {
        int count = 0;
        Date current = d1;
        while (current < d2) {
            if (is_business_day(current)) {
                ++count;
            }
            ++current;
        }
        return count;
    }

    [[nodiscard]] static constexpr bool is_weekend(Date d) noexcept {
        const int dow = d.day_of_week();
        return dow == 0 || dow == 6;
    }

private:
    [[nodiscard]] constexpr Date adjust_following(Date d) const noexcept {
        Date result = d;
        while (!is_business_day(result)) {
            ++result;
        }
        return result;
    }

    [[nodiscard]] constexpr Date adjust_modified_following(Date d) const noexcept {
        Date result = adjust_following(d);
        if (result.month() != d.month()) {
            result = adjust_preceding(d);
        }
        return result;
    }

    [[nodiscard]] constexpr Date adjust_preceding(Date d) const noexcept {
        Date result = d;
        while (!is_business_day(result)) {
            --result;
        }
        return result;
    }

    [[nodiscard]] constexpr Date adjust_modified_preceding(Date d) const noexcept {
        Date result = adjust_preceding(d);
        if (result.month() != d.month()) {
            result = adjust_following(d);
        }
        return result;
    }
};

// Basic creation of struc type calendar
/// @brief A calendar that only consider weekends as holidays
struct WeekendsOnly : CalendarBase<WeekendsOnly> {
    [[nodiscard]] static constexpr bool is_business_day_impl(Date d) noexcept {
        return !is_weekend(d);
    }
};

/// @brief Calendrier TARGET
struct TargetCalendar : CalendarBase<TargetCalendar> {
    [[nodiscard]] static constexpr bool is_business_day_impl(Date d) noexcept {
        if (is_weekend(d)) return false;

        const Date::YMD x = d.ymd();

        // Fixed dates
        if (x.m == 1  && x.d == 1)  return false; // New year
        if (x.m == 5  && x.d == 1)  return false; // Labor day
        if (x.m == 12 && x.d == 25) return false; // Christmas
        if (x.m == 12 && x.d == 26) return false; // Boxing day

        if (x.m == 3 || x.m == 4) {
            const Date easter = easter_sunday(x.y);
            if (d == easter - 2) return false; // Good Friday
            if (d == easter + 1) return false; // Easter Monday
        }
        return true;
    }
};

/// @brief Basic calendar with all the days considered as business day  (24/7)
struct NullCalendar : CalendarBase<NullCalendar> {
    [[nodiscard]] static constexpr bool is_business_day_impl(Date) noexcept {
        return true;
    }
};

} // namespace Core

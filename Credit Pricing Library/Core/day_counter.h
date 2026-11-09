
#pragma once
#include <concepts>

#include "Dates.h"


namespace  Core {

    /// @brief Static interface for the day_counter, open-closed principle
    /// Static polymorphism to avoid overhead in potential loops when pricing
    template<typename Derived>
    struct DayCounter {
        /// @brief Function to compute the number of days between to dates
        [[nodiscard]] constexpr int day_count(Date d1, Date d2) const noexcept {
            return static_cast<const Derived*>(this)->day_count_impl(d1, d2);
        }
        /// @brief Function to compute the year_fraction based on derived type
        [[nodiscard]] constexpr double year_fraction(Date d1, Date d2) const noexcept {
            return static_cast<const Derived*>(this)->year_fraction_impl(d1, d2);
        }
    };

    template <class T>
    concept DayCountConvention = requires(const T& dc, Date d1, Date d2) {
        { dc.day_count(d1, d2) } -> std::same_as<int>;
        { dc.year_fraction(d1, d2) } -> std::same_as<double>;
    };

    ///@brief Actual /360 day count convention
    struct Actual360 : DayCounter<Actual360> {
        [[nodiscard]] static constexpr int day_count_impl(Date d1, Date d2) noexcept {
            return d2 - d1;
        }
        [[nodiscard]] static constexpr double year_fraction_impl(Date d1, Date d2) noexcept {
            return static_cast<double>(d2 - d1) / 360.0;
        }
    };

    ///@brief Actual / 365 (Fixed)
    struct Actual365 : DayCounter<Actual365> {
        [[nodiscard]] static constexpr int day_count_impl(Date d1, Date d2) noexcept {
            return d2 - d1;
        }
        [[nodiscard]] static constexpr double year_fraction_impl(Date d1, Date d2) noexcept {
            return static_cast<double>(d2 - d1) / 365.0;
        }
    };

    using Actual365Fixed = Actual365;

    ///@brief Actual /Actual (ISDA)
    struct ActualActual_ISDA : DayCounter<ActualActual_ISDA> {
        [[nodiscard]] static constexpr int day_count_impl(Date d1, Date d2) noexcept {
            return d2 - d1;
        }

        [[nodiscard]] static constexpr double year_fraction_impl(Date d1, Date d2) noexcept {
            if (d1 == d2) return 0.0;
            if (d2 < d1) return -year_fraction_impl(d2, d1);

            const int y1 = d1.year();
            const int y2 = d2.year();
            if (y1 == y2) {
                return static_cast<double>(d2 - d1) / yc_basis(y1);
            }

            const Date first_jan_next = Date::from_ymd_unchecked(y1 + 1, 1, 1);
            const Date first_jan_last = Date::from_ymd_unchecked(y2, 1, 1);
            return static_cast<double>(first_jan_next - d1) / yc_basis(y1)
                 + static_cast<double>(y2 - y1 - 1)
                 + static_cast<double>(d2 - first_jan_last) / yc_basis(y2);
        }

    private :
        [[nodiscard]] static constexpr double yc_basis(int year) noexcept {
            return Date::is_leap_year(year) ? 366.0 : 365.0;
        }
    };

    /// @brief 30/360 Bond Basis (ISDA 2006)
    struct Thirty360 : DayCounter<Thirty360> {
        [[nodiscard]] static constexpr int day_count_impl(Date d1, Date d2) noexcept {
            const Date::YMD a = d1.ymd();
            const Date::YMD b = d2.ymd();
            const int dd1 = (a.d == 31) ? 30 : a.d;
            const int dd2 = (b.d == 31 && dd1 == 30) ? 30 : b.d;
            return 360 * (b.y - a.y) + 30 * (b.m - a.m) + (dd2 - dd1);
        }
        [[nodiscard]] static constexpr double year_fraction_impl(Date d1, Date d2) noexcept {
            return static_cast<double>(day_count_impl(d1, d2)) / 360.0;
        }
    };

    /// @brief 30E/360 (Eurobond Basis, ISDA 2006)
    struct Thirty360European : DayCounter<Thirty360European> {
        [[nodiscard]] static constexpr int day_count_impl(Date d1, Date d2) noexcept {
            const Date::YMD a = d1.ymd();
            const Date::YMD b = d2.ymd();
            const int dd1 = (a.d == 31) ? 30 : a.d;
            const int dd2 = (b.d == 31) ? 30 : b.d;
            return 360 * (b.y - a.y) + 30 * (b.m - a.m) + (dd2 - dd1);
        }
        [[nodiscard]] static constexpr double year_fraction_impl(Date d1, Date d2) noexcept {
            return static_cast<double>(day_count_impl(d1, d2)) / 360.0;
        }
    };

}

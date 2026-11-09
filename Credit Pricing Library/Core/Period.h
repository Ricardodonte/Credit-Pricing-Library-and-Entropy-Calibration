// Periods ("3M", "5Y", ...), coupon frequencies and date + period arithmetic.

#pragma once
 #include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Dates.h"

namespace Core {

    enum class TimeUnit { Days, Weeks, Months, Years };

    /// @brief A length of time expressed in calendar units: "2D", "1W", "3M", "5Y".

    class Period {
    public:
        constexpr Period() noexcept = default;
        constexpr Period(int length, TimeUnit unit) noexcept : length_(length), unit_(unit) {}

        [[nodiscard]] constexpr int length() const noexcept { return length_; }
        [[nodiscard]] constexpr TimeUnit unit() const noexcept { return unit_; }

        /// @brief Length in months. Only defined for months-years periods.
        [[nodiscard]] constexpr int months() const {
            if (unit_ == TimeUnit::Months) return length_;
            if (unit_ == TimeUnit::Years)  return 12 * length_;
            throw std::invalid_argument("Period: only month or year periods can be expressed in months");
        }

        [[nodiscard]] constexpr Period operator-() const noexcept { return {-length_, unit_}; }
        [[nodiscard]] constexpr Period operator*(int n) const noexcept { return {length_ * n, unit_}; }
        [[nodiscard]] constexpr bool operator==(const Period&) const noexcept = default;

        /// @brief Parses "3M", "5y", "-1W", "2D". Throws std::invalid_argument on malformed input.
        [[nodiscard]] static Period parse(std::string_view text) {
            const auto fail = [&]() -> Period {
                throw std::invalid_argument("Period: invalid period '" + std::string(text) + "'");
            };
            std::size_t i = 0;
            const bool negative = !text.empty() && text[0] == '-';
            if (negative) i = 1;
            if (text.size() < i + 2) return fail();

            int value = 0;
            for (; i + 1 < text.size(); ++i) {
                const auto c = static_cast<unsigned char>(text[i]);
                if (!std::isdigit(c)) return fail();
                value = 10 * value + (c - '0');
            }
            if (negative) value = -value;

            switch (std::toupper(static_cast<unsigned char>(text.back()))) {
                case 'D': return {value, TimeUnit::Days};
                case 'W': return {value, TimeUnit::Weeks};
                case 'M': return {value, TimeUnit::Months};
                case 'Y': return {value, TimeUnit::Years};
                default:  return fail();
            }
        }

        [[nodiscard]] std::string to_string() const {
            constexpr char suffix[] = {'D', 'W', 'M', 'Y'};
            return std::to_string(length_) + suffix[static_cast<int>(unit_)];
        }

    private:
        int length_ = 0;
        TimeUnit unit_ = TimeUnit::Days;
    };

    /// @brief Coupon frequency (number of payments per year).
    enum class Frequency { Annual = 1, SemiAnnual = 2, Quarterly = 4, Monthly = 12 };

    [[nodiscard]] constexpr Period to_period(Frequency f) noexcept {
        return {12 / static_cast<int>(f), TimeUnit::Months};
    }

    /// @brief Date + period (unadjusted). Month and year periods are clamped to the end of the month
    ///        (31/01 + 1M = 28/02). With end_of_month = true, an end-of-month date stays an end of month.
    [[nodiscard]] constexpr Date add_period(Date d, Period p, bool end_of_month = false) {
        switch (p.unit()) {
            case TimeUnit::Days:   return d + p.length();
            case TimeUnit::Weeks:  return d + 7 * p.length();
            case TimeUnit::Months: return d.add_months(p.length(), end_of_month);
            case TimeUnit::Years:  return d.add_years(p.length(), end_of_month);
        }
        return d;
    }

    [[nodiscard]] constexpr Date operator+(Date d, Period p) { return add_period(d, p); }
    [[nodiscard]] constexpr Date operator-(Date d, Period p) { return add_period(d, -p); }

} // namespace Core

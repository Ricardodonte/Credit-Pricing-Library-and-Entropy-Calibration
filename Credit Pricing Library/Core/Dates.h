
#pragma once
#include <compare>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>


namespace Core {

/// @brief Date class represented as a integer (number of days since 1970-01-01).
///        Light value type (4 bytes), immuable, copiable, and constexpr-friendly.
///        Algorithm based on the sur Howard Hinnant's date algorithms.
///        Supported range for years : années 1 à 9999.
class Date {
public:
    struct YMD { int y, m, d; };

    /// @brief Default constructor : date nulle
    /// Build the null date INT32_MIN, invalid date type
    constexpr Date() noexcept : days_(null_days) {}

    /// @brief Year/Month/Day constructor.
    constexpr Date(int year, int month, int day)
        : days_(checked_ymd_to_days(year, month, day)) {}

    /// @brief Constructor based on the number of days since the base date.
    explicit constexpr Date(std::int32_t days_since_epoch) noexcept
        : days_(days_since_epoch) {}

    /// @brief Construction with no need for validation, to avoid the usage in a no exept function , require (y,m,d) valid thought

    [[nodiscard]] static constexpr Date from_ymd_unchecked(int y, int m, int d) noexcept {
        return Date(ymd_to_days(y, m, d));
    }

    // --- Getters ---
    [[nodiscard]] constexpr int year() const noexcept {
        return days_to_ymd(days_).y;
    }
    [[nodiscard]] constexpr int month() const noexcept {
        return days_to_ymd(days_).m;
    }
    [[nodiscard]] constexpr int day() const noexcept {
        return days_to_ymd(days_).d;
    }
    [[nodiscard]] constexpr YMD ymd() const noexcept {
        return days_to_ymd(days_);
    }

    /// @brief Weekdays checker (0 = Sunday , 6 = Saturday).

    [[nodiscard]] constexpr int day_of_week() const noexcept {
        const int r = (days_ + 4) % 7;  // 1970-01-01 was a Thursday
        return r < 0 ? r + 7 : r;
    }

    // --- Calendar helpers --- Calendar tools needed for conventions calculations and future flows dates

    [[nodiscard]] static constexpr bool is_leap_year(int y) noexcept {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }
    [[nodiscard]] static constexpr int days_in_month(int y, int m) noexcept {
        constexpr int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return (m == 2 && is_leap_year(y)) ? 29 : dim[m - 1];
    }
    [[nodiscard]] static constexpr bool is_valid(int y, int m, int d) noexcept {
        return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m);
    }
    [[nodiscard]] constexpr bool is_end_of_month() const noexcept {
        const YMD x = ymd();
        return x.d == days_in_month(x.y, x.m);
    }
    [[nodiscard]] constexpr Date end_of_month() const {
        const YMD x = ymd();
        return {x.y, x.m, days_in_month(x.y, x.m)};
    }

    /// @brief A method for adding months, while taking into account almost all the specifities we can encounter

    [[nodiscard]] constexpr Date add_months(int n, bool end_of_month_rule = false) const {
        const YMD x = ymd();
        const int total = x.y * 12 + (x.m - 1) + n;
        int y = total / 12;
        int m = total % 12;
        if (m < 0) { m += 12; --y; }
        ++m;
        const int dim = days_in_month(y, m);
        const int d = (end_of_month_rule && x.d == days_in_month(x.y, x.m)) ? dim
                    : (x.d < dim ? x.d : dim);
        return {y, m, d};
    }
    [[nodiscard]] constexpr Date add_years(int n, bool end_of_month_rule = false) const {
        return add_months(12 * n, end_of_month_rule);
    }

    // --- Arithmetics and operators ---

    [[nodiscard]] constexpr Date operator+(int days) const noexcept {
        return Date(days_ + days);
    }
    [[nodiscard]] constexpr Date operator-(int days) const noexcept {
        return Date(days_ - days);
    }
    [[nodiscard]] constexpr int operator-(Date other) const noexcept {
        return days_ - other.days_;
    }

    constexpr Date& operator+=(int days) noexcept {
        days_ += days;
        return *this;
    }
    constexpr Date& operator-=(int days) noexcept {
        days_ -= days;
        return *this;
    }

    constexpr Date& operator++() noexcept {
        ++days_;
        return *this;
    }
    constexpr Date operator++(int) noexcept {
        Date temp = *this;
        ++days_;
        return temp;
    }

    constexpr Date& operator--() noexcept {
        --days_;
        return *this;
    }
    constexpr Date operator--(int) noexcept {
        Date temp = *this;
        --days_;
        return temp;
    }

    // --- Comparisons (C++20 spaceship operator) ---
    [[nodiscard]] constexpr auto operator<=>(const Date&) const noexcept = default;

    // --- Validity and guards ---

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return days_ == null_days;
    }
    [[nodiscard]] static constexpr Date null_date() noexcept {
        return Date(null_days);
    }

    // -- Raw date access ---
    [[nodiscard]] constexpr std::int32_t raw_days() const noexcept {
        return days_;
    }

    // --- To string display ---
    [[nodiscard]] std::string to_string() const {
        if (is_null()) return "null";
        const YMD x = ymd();
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << x.y << "-"
            << std::setw(2) << x.m << "-"
            << std::setw(2) << x.d;
        return oss.str();
    }

    // --- Parsing from string (format YYYY-MM-DD) ---
    [[nodiscard]] static Date from_string(const std::string& str) {
        int y = 0, m = 0, d = 0;
        char sep1 = 0, sep2 = 0;
        std::istringstream iss(str);
        iss >> y >> sep1 >> m >> sep2 >> d;
        if (iss.fail() || sep1 != '-' || sep2 != '-') {
            throw std::invalid_argument("Invalid date format: " + str);
        }
        iss >> std::ws;
        if (!iss.eof()) {
            throw std::invalid_argument("Invalid date format (trailing characters): " + str);
        }
        if (!is_valid(y, m, d)) {
            throw std::invalid_argument("Invalid calendar date: " + str);
        }
        return {y, m, d};
    }

private:
    static constexpr std::int32_t null_days = std::numeric_limits<std::int32_t>::min();

    std::int32_t days_;  // number of days from 1970-01-01 (epoch UNIX)


    static constexpr std::int32_t checked_ymd_to_days(int y, int m, int d) {
        if (!is_valid(y, m, d)) {
            throw std::invalid_argument("Date: invalid year/month/day");
        }
        return ymd_to_days(y, m, d);
    }

    /// @brief Converts a civil date to the number of days since the Unix epoch.
    ///         Howard Hinnant algorithm (days_from_civil).
    static constexpr std::int32_t ymd_to_days(int y, int m, int d) noexcept {
        y -= (m <= 2);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const auto yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int>(doe) - 719468;
    }

    /// @brief Converts a number of days since the Unix epoch to a civil date
    ///         Howard Hinnant algorithm (civil_from_days).
    static constexpr YMD days_to_ymd(std::int32_t z) noexcept {
        z += 719468;
        const int era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int y = static_cast<int>(yoe) + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp  = (5 * doy + 2) / 153;
        const unsigned d   = doy - (153 * mp + 2) / 5 + 1;
        const unsigned m   = mp < 10 ? mp + 3 : mp - 9;
        return {y + (m <= 2), static_cast<int>(m), static_cast<int>(d)};
    }
};

} // namespace Core

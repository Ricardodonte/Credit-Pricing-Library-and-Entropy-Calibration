// IMM dates (20th of March, June, September, December) and CDS standard maturities handling here

#pragma once
#include "Dates.h"
#include "Period.h"

namespace Core {

    /// @brief True for the 20th of March, June, September and December.
    [[nodiscard]] constexpr bool is_imm_date(Date d) noexcept {
        const Date::YMD x = d.ymd();
        return x.d == 20 && x.m % 3 == 0;
    }

    /// @brief Latest IMM date on or before d.
    [[nodiscard]] constexpr Date previous_imm_date(Date d) noexcept {
        const Date::YMD x = d.ymd();
        int m = x.m - x.m % 3;
        if (m == x.m && x.d < 20) m -= 3;
        if (m <= 0) return Date::from_ymd_unchecked(x.y - 1, 12, 20);
        return Date::from_ymd_unchecked(x.y, m, 20);
    }

    /// @brief Earliest IMM date strictly after d.
    [[nodiscard]] constexpr Date next_imm_date(Date d) noexcept {
        const Date::YMD x = d.ymd();
        int m = x.m + (3 - x.m % 3) % 3;
        if (m == x.m && x.d >= 20) m += 3;
        if (m > 12) return Date::from_ymd_unchecked(x.y + 1, 3, 20);
        return Date::from_ymd_unchecked(x.y, m, 20);
    }

    /// @brief Standard CDS maturity, ISDA 2015 rule (semi-annual roll on 20 March / 20 September).
    ///        Trade dates in [20 Mar, 20 Sep[ use 20 June as anchor, trade dates in [20 Sep, 20 Mar[
    ///        use 20 December; maturity = anchor + tenor (unadjusted). Tenor in months or years.
    [[nodiscard]] constexpr Date cds_standard_maturity(Date trade_date, Period tenor) {
        const int y = trade_date.year();
        Date anchor;
        if (trade_date < Date(y, 3, 20))      anchor = Date(y - 1, 12, 20);
        else if (trade_date < Date(y, 9, 20)) anchor = Date(y, 6, 20);
        else                                  anchor = Date(y, 12, 20);
        return anchor.add_months(tenor.months());
    }

} // namespace Core

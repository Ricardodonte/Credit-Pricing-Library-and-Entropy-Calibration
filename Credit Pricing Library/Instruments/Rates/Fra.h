// Forward rate agreements and rate futures (converted to an equivalent FRA).

#pragma once
#include <stdexcept>

#include "Core/Dates.h"
#include "Core/Period.h"
#include "Core/calendar.h"
#include "Core/day_counter.h"

namespace Instruments::Rates {

    /// @brief Forward rate agreement on [start, maturity]: fair when 1 + rate * accrual = DF(start) / DF(maturity).
    struct Fra {
        Core::Date start;
        Core::Date maturity;
        double accrual;  ///< year fraction start -> maturity under the FRA day count
        double rate;     ///< FRA rate (for futures: convexity-adjusted forward rate)
    };

    /// @brief "start_tenor x end_tenor" FRA, both tenors measured from spot (e.g. 6M x 9M).
    /// @param start_tenor spot -> start of the forward period (6M in a 6x9)
    /// @param end_tenor   spot -> end of the forward period (9M in a 6x9)
    template <class Cal, Core::DayCountConvention DC = Core::Actual360>
    [[nodiscard]] Fra make_fra(Core::Date trade_date, Core::Period start_tenor, Core::Period end_tenor, double rate,
                               const Cal& calendar, DC day_counter = {}, int settlement_days = 2,
                               Core::BusinessDayConvention convention =
                                   Core::BusinessDayConvention::ModifiedFollowing) {
        const Core::Date spot = calendar.advance(trade_date, settlement_days);
        const Core::Date start = calendar.adjust(Core::add_period(spot, start_tenor), convention);
        const Core::Date maturity = calendar.adjust(Core::add_period(spot, end_tenor), convention);
        if (!(start < maturity))
            throw std::invalid_argument("make_fra: end tenor must be after start tenor");
        return {start, maturity, day_counter.year_fraction(start, maturity), rate};
    }

    /// @brief Rate future (price convention 100 - rate) converted to a FRA.
    ///        The convexity adjustment is the Ho-Lee one: 0.5 * sigma^2 * t1 * t2, with t1 / t2 the
    ///        Act/365F times from trade date to start / maturity. sigma = 0 disables it.
    /// @param price                futures price, e.g. 97.85 for a 2.15% rate
    /// @param convexity_volatility short-rate normal volatility sigma (e.g. 0.01), 0 = no adjustment
    template <Core::DayCountConvention DC = Core::Actual360>
    [[nodiscard]] Fra make_future(Core::Date trade_date, Core::Date start, Core::Date maturity, double price,
                                  double convexity_volatility = 0.0, DC day_counter = {}) {
        if (!(trade_date <= start && start < maturity))
            throw std::invalid_argument("make_future: dates must satisfy trade <= start < maturity");
        constexpr Core::Actual365Fixed time_axis{};
        const double t1 = time_axis.year_fraction(trade_date, start);
        const double t2 = time_axis.year_fraction(trade_date, maturity);
        const double futures_rate = (100.0 - price) / 100.0;
        const double convexity = 0.5 * convexity_volatility * convexity_volatility * t1 * t2;
        return {start, maturity, day_counter.year_fraction(start, maturity), futures_rate - convexity};
    }

} // namespace Instruments::Rates

// Vanilla fixed-vs-floating interest rate swap (single-curve framework).

#pragma once
#include <vector>

#include "Core/Dates.h"
#include "Core/Period.h"
#include "Core/Schedule.h"
#include "Core/calendar.h"
#include "Core/day_counter.h"

namespace Instruments::Rates {

    struct FixedCoupon {
        Core::Date accrual_start;
        Core::Date accrual_end;
        Core::Date payment;
        double accrual;  ///< year fraction under the fixed-leg day count
    };

    /// @brief Fixed-vs-floating swap. In a single-curve framework (e.g. an OIS curve used both for
    ///        projection and discounting) the floating leg is worth DF(start) - DF(maturity), so only
    ///        the fixed leg needs an explicit schedule.
    struct Swap {
        Core::Date start;
        Core::Date maturity;
        double fixed_rate;
        std::vector<FixedCoupon> fixed_leg;
    };

    /// @brief Spot-starting swap of the given tenor. Defaults: annual Act/360 fixed leg (EUR OIS
    ///        convention), T+2, Modified Following, schedule generated backward from maturity.
    /// @param fixed_rate  quoted par swap rate (becomes the fixed coupon)
    /// @param fixed_tenor period between two fixed coupons (1Y annual, 6M semi-annual)
    template <class Cal, Core::DayCountConvention DC = Core::Actual360>
    [[nodiscard]] Swap make_swap(Core::Date trade_date, Core::Period tenor, double fixed_rate, const Cal& calendar,
                                 Core::Period fixed_tenor = {1, Core::TimeUnit::Years}, DC day_counter = {},
                                 int settlement_days = 2,
                                 Core::BusinessDayConvention convention =
                                     Core::BusinessDayConvention::ModifiedFollowing) {
        const Core::Date start = calendar.advance(trade_date, settlement_days);
        const Core::Schedule schedule = Core::make_schedule(start, Core::add_period(start, tenor), fixed_tenor,
                                                            calendar, convention, convention,
                                                            Core::DateGenerationRule::Backward);
        Swap swap{schedule.front(), schedule.back(), fixed_rate, {}};
        swap.fixed_leg.reserve(schedule.size() - 1);
        for (std::size_t i = 1; i < schedule.size(); ++i) {
            swap.fixed_leg.push_back({schedule[i - 1], schedule[i], schedule[i],
                                      day_counter.year_fraction(schedule[i - 1], schedule[i])});
        }
        return swap;
    }

} // namespace Instruments::Rates

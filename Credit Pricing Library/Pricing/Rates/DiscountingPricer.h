// Discounting pricer for linear rate instruments (deposits, FRAs, swaps).
// Every function is templated on a discount function DF(Core::Date) -> double rather than on a curve:
// the very same code prices on a finished curve (discount_function(curve)) and inside the bootstrap
// objective (curve completed by a candidate pillar), which guarantees that a bootstrapped curve reprices
// its inputs exactly.

#pragma once
#include <cstddef>

#include "Core/Dates.h"
#include "Instruments/Rates/Deposit.h"
#include "Instruments/Rates/Fra.h"
#include "Instruments/Rates/Swap.h"

namespace Pricing::Rates {

    /// @brief Discount function of a curve exposing discount(Core::Date). The curve must outlive it.
    template <class Curve>
    [[nodiscard]] auto discount_function(const Curve& curve) {
        return [&curve](Core::Date d) { return curve.discount(d); };
    }

    template <class DF>
    [[nodiscard]] double implied_rate(const Instruments::Rates::Deposit& deposit, DF&& df) {
        return (df(deposit.start) / df(deposit.maturity) - 1.0) / deposit.accrual;
    }

    template <class DF>
    [[nodiscard]] double implied_rate(const Instruments::Rates::Fra& fra, DF&& df) {
        return (df(fra.start) / df(fra.maturity) - 1.0) / fra.accrual;
    }

    /// @brief Fixed-leg annuity sum(accrual_i * DF(payment_i)), from coupon `first` onwards.
    template <class DF>
    [[nodiscard]] double annuity(const Instruments::Rates::Swap& swap, DF&& df, std::size_t first = 0) {
        double a = 0.0;
        for (std::size_t i = first; i < swap.fixed_leg.size(); ++i)
            a += swap.fixed_leg[i].accrual * df(swap.fixed_leg[i].payment);
        return a;
    }

    /// @brief Single-curve floating leg: DF(start) - DF(maturity).
    template <class DF>
    [[nodiscard]] double floating_leg(const Instruments::Rates::Swap& swap, DF&& df) {
        return df(swap.start) - df(swap.maturity);
    }

    template <class DF>
    [[nodiscard]] double par_rate(const Instruments::Rates::Swap& swap, DF&& df) {
        return floating_leg(swap, df) / annuity(swap, df);
    }

    /// @brief NPV per unit notional for the fixed-rate receiver.
    template <class DF>
    [[nodiscard]] double npv_receive_fixed(const Instruments::Rates::Swap& swap, DF&& df) {
        return swap.fixed_rate * annuity(swap, df) - floating_leg(swap, df);
    }

} // namespace Pricing::Rates

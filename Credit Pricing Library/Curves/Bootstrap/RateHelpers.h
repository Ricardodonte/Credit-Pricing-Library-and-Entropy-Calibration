// Bootstrap helpers for the yield curve: deposits, FRAs / futures, swaps.
// The unknown is the discount factor at the helper's pillar (its maturity).

#pragma once
#include <cstddef>
#include <string>

#include "Core/Dates.h"
#include "Instruments/Rates/Deposit.h"
#include "Instruments/Rates/Fra.h"
#include "Instruments/Rates/Swap.h"
#include "Pricing/Rates/DiscountingPricer.h"

namespace Curves::Bootstrap {

    namespace detail {
        /// @brief Discount function of `curve` completed by a candidate pillar (t_new, df_new).
        ///        Pure: the curve is only read (principle 3 of the guide).
        /// @param t_new  time (curve axis) of the pillar being solved
        /// @param df_new candidate discount factor at t_new, proposed by the solver
        template <class Curve>
        [[nodiscard]] auto candidate_discount(const Curve& curve, double t_new, double df_new) {
            return [&curve, t_new, df_new](Core::Date d) {
                return curve.discount_with_candidate(curve.time_from_reference(d), t_new, df_new);
            };
        }
    }

    // Note: objectives keep a reference to the curve (and to the helper for SwapHelper). They are built and
    // consumed within one step of the bootstrap and must not outlive it.

    class DepositHelper {
    public:
        explicit DepositHelper(Instruments::Rates::Deposit deposit) noexcept : deposit_(deposit) {}

        [[nodiscard]] Core::Date pillar_date() const noexcept { return deposit_.maturity; }
        [[nodiscard]] double quote() const noexcept { return deposit_.rate; }
        [[nodiscard]] std::string description() const { return "Deposit " + deposit_.maturity.to_string(); }
        [[nodiscard]] const Instruments::Rates::Deposit& instrument() const noexcept { return deposit_; }

        template <class Curve>
        [[nodiscard]] double implied_quote(const Curve& curve) const {
            return Pricing::Rates::implied_rate(deposit_, Pricing::Rates::discount_function(curve));
        }

        template <class Curve>
        [[nodiscard]] auto objective(const Curve& curve) const {
            const double t_new = curve.time_from_reference(pillar_date());
            return [&curve, t_new, deposit = deposit_](double df_new) {
                return Pricing::Rates::implied_rate(deposit, detail::candidate_discount(curve, t_new, df_new))
                     - deposit.rate;
            };
        }

    private:
        Instruments::Rates::Deposit deposit_;
    };

    class FraHelper {
    public:
        explicit FraHelper(Instruments::Rates::Fra fra) noexcept : fra_(fra) {}

        [[nodiscard]] Core::Date pillar_date() const noexcept { return fra_.maturity; }
        [[nodiscard]] double quote() const noexcept { return fra_.rate; }
        [[nodiscard]] std::string description() const {
            return "FRA " + fra_.start.to_string() + " -> " + fra_.maturity.to_string();
        }
        [[nodiscard]] const Instruments::Rates::Fra& instrument() const noexcept { return fra_; }

        template <class Curve>
        [[nodiscard]] double implied_quote(const Curve& curve) const {
            return Pricing::Rates::implied_rate(fra_, Pricing::Rates::discount_function(curve));
        }

        template <class Curve>
        [[nodiscard]] auto objective(const Curve& curve) const {
            const double t_new = curve.time_from_reference(pillar_date());
            return [&curve, t_new, fra = fra_](double df_new) {
                return Pricing::Rates::implied_rate(fra, detail::candidate_discount(curve, t_new, df_new)) - fra.rate;
            };
        }

    private:
        Instruments::Rates::Fra fra_;
    };

    class SwapHelper {
    public:
        explicit SwapHelper(Instruments::Rates::Swap swap) : swap_(std::move(swap)) {}

        [[nodiscard]] Core::Date pillar_date() const noexcept { return swap_.maturity; }
        [[nodiscard]] double quote() const noexcept { return swap_.fixed_rate; }
        [[nodiscard]] std::string description() const { return "Swap " + swap_.maturity.to_string(); }
        [[nodiscard]] const Instruments::Rates::Swap& instrument() const noexcept { return swap_; }

        template <class Curve>
        [[nodiscard]] double implied_quote(const Curve& curve) const {
            return Pricing::Rates::par_rate(swap_, Pricing::Rates::discount_function(curve));
        }

        /// @brief Residual = par rate(candidate) - quote. The coupons paid on or before the last known pillar
        ///        do not depend on the candidate: their annuity is computed once, before the solver.
        template <class Curve>
        [[nodiscard]] auto objective(const Curve& curve) const {
            const double t_new = curve.time_from_reference(pillar_date());
            const Core::Date last_known = curve.pillar_dates().back();
            const auto df = Pricing::Rates::discount_function(curve);

            std::size_t first_unknown = 0;
            double known_annuity = 0.0;
            while (first_unknown < swap_.fixed_leg.size() && swap_.fixed_leg[first_unknown].payment <= last_known) {
                known_annuity += swap_.fixed_leg[first_unknown].accrual * df(swap_.fixed_leg[first_unknown].payment);
                ++first_unknown;
            }

            return [&curve, t_new, known_annuity, first_unknown, swap = &swap_](double df_new) {
                const auto df_candidate = detail::candidate_discount(curve, t_new, df_new);
                const double annuity = known_annuity + Pricing::Rates::annuity(*swap, df_candidate, first_unknown);
                return Pricing::Rates::floating_leg(*swap, df_candidate) / annuity - swap->fixed_rate;
            };
        }

    private:
        Instruments::Rates::Swap swap_;
    };

} // namespace Curves::Bootstrap

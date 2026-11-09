// CDS prepared for repeated pricing: the integration grid is built once.
// Everything that depends on the contract and on the yield curve (grid nodes, discount factors, accrual
// fractions, DF to cash settlement) is computed at construction; value() then only evaluates the survival
// probabilities on the grid, without any allocation. Typical use: pricing the same CDS on many credit curves
// (scenarios, shocked or re-bootstrapped curves) with a fixed yield curve.
// The yield curve is NOT kept: if it changes, the CDS must be prepared again.
// The grid is exact only for credit curves whose pillars inside ]protection start, maturity[ are those given
// at construction: value() checks it and throws otherwise.

#pragma once
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Core/Dates.h"
#include "Instruments/Credit/Cds.h"
#include "Pricing/Credit/CdsIntegrationGrid.h"
#include "Pricing/Credit/CdsValuation.h"

namespace Pricing::Credit {

    class PreparedCds {
    public:
        /// @param yield_curve must be built as of the CDS trade date; used only during construction.
        /// @param credit_pillars pillar dates of the credit curves this CDS will be priced on.
        template <class YieldCurveT>
        PreparedCds(Instruments::Credit::Cds cds, const YieldCurveT& yield_curve,
                    std::span<const Core::Date> credit_pillars)
            : cds_(std::move(cds)),
              grid_(cds_, yield_curve, credit_pillars),
              df_cash_settlement_(yield_curve.discount(cds_.cash_settlement_date)) {
            if (yield_curve.reference_date() != cds_.trade_date)
                throw std::invalid_argument("PreparedCds: the yield curve must be built as of the CDS trade date");
            for (const Core::Date d : credit_pillars)
                if (is_inside(d)) credit_pillars_.push_back(d);
        }

        [[nodiscard]] const Instruments::Credit::Cds& contract() const noexcept { return cds_; }

        /// @brief Credit pillars inside the protection period that the grid was built with.
        [[nodiscard]] std::span<const Core::Date> credit_pillars() const noexcept { return credit_pillars_; }

        /// @brief Values the CDS on a credit curve built as of the trade date, whose pillars inside the
        ///        protection period are the prepared ones.
        template <class CreditCurveT>
        [[nodiscard]] CdsValuation value(const CreditCurveT& credit_curve) const {
            if (credit_curve.reference_date() != cds_.trade_date)
                throw std::invalid_argument("PreparedCds: the credit curve must be built as of the CDS trade date");
            if (!same_pillars(credit_curve.pillar_dates()))
                throw std::invalid_argument("PreparedCds: credit curve pillars differ from the prepared ones");

            const auto nodes = grid_.nodes();
            const CdsLegPv legs = grid_.accumulate(
                [&](std::size_t j) { return credit_curve.survival_probability(nodes[j]); }, 0, grid_.size() - 1);
            return CdsValuation::from_legs(cds_, legs, df_cash_settlement_);
        }

    private:
        [[nodiscard]] bool is_inside(Core::Date d) const noexcept {
            return cds_.protection_start < d && d < cds_.maturity;
        }

        // Compares the pillars inside the protection period, without allocating.
        [[nodiscard]] bool same_pillars(std::span<const Core::Date> pillars) const noexcept {
            std::size_t k = 0;
            for (const Core::Date d : pillars) {
                if (!is_inside(d)) continue;
                if (k == credit_pillars_.size() || credit_pillars_[k] != d) return false;
                ++k;
            }
            return k == credit_pillars_.size();
        }

        Instruments::Credit::Cds cds_;
        CdsIntegrationGrid grid_;
        double df_cash_settlement_;
        std::vector<Core::Date> credit_pillars_;
    };

} // namespace Pricing::Credit

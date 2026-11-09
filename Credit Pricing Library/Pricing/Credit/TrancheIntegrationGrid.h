// Time discretisation of the tranche legs, reduced to two weight vectors on the grid nodes.
// With E_j = E[min(L(t_j), K)] the expected base-tranche loss at node j and O_j = K - E_j the expected
// outstanding notional, both legs of a base tranche [0, K] are LINEAR in the node values:
//     protection = sum_segments 1/2 (P_a + P_b) (E_b - E_a)                    = sum_j protection_weight_j E_j
//     premium    = sum_periods P(pay) accrual x (average of O over the period) = sum_j premium_weight_j O_j
// (trapezoidal rules; the outstanding is flat before the protection start and after maturity, so the first
// period accrues on the full notional before the trade date and the last one includes the maturity day).
// The weights only depend on the contract and the yield curve: they are computed once, and pricing a base
// tranche is two dot products.

#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "Core/Dates.h"
#include "Core/Period.h"
#include "Instruments/Credit/Tranche.h"

namespace Pricing::Credit {

    struct TrancheGridSettings {
        Core::Period step{3, Core::TimeUnit::Months};  ///< maximal spacing of the default-leg nodes (1M: finer)
    };

    class TrancheIntegrationGrid {
    public:
        template <class YieldCurveT>
        TrancheIntegrationGrid(const Instruments::Credit::Tranche& tranche, const YieldCurveT& yield_curve,
                               const TrancheGridSettings& settings = {}) {
            if (tranche.periods.empty())
                throw std::invalid_argument("TrancheIntegrationGrid: tranche has no premium period");
            if (settings.step.length() <= 0)
                throw std::invalid_argument("TrancheIntegrationGrid: the grid step must be positive");
            const Core::Date start = tranche.protection_start;
            const Core::Date end = tranche.maturity;
            if (!(start < end))
                throw std::invalid_argument("TrancheIntegrationGrid: protection start must be before maturity");

            build_nodes(tranche, settings.step);
            const std::size_t n = nodes_.size();
            std::vector<double> df(n);
            for (std::size_t j = 0; j < n; ++j) df[j] = yield_curve.discount(nodes_[j]);

            protection_weight_.assign(n, 0.0);
            for (std::size_t j = 0; j + 1 < n; ++j) {
                const double h = 0.5 * (df[j] + df[j + 1]);
                protection_weight_[j] -= h;
                protection_weight_[j + 1] += h;
            }

            premium_weight_.assign(n, 0.0);
            for (const auto& p : tranche.periods) {
                const double per_day = p.accrual * yield_curve.discount(p.payment)
                                     / static_cast<double>(p.accrual_end - p.accrual_start);
                const Core::Date lo = std::max(p.accrual_start, start);
                const Core::Date hi = std::min(p.accrual_end, end);
                if (p.accrual_start < start)  // before the trade date: no loss yet
                    premium_weight_.front() += per_day * static_cast<double>(std::min(p.accrual_end, start) - p.accrual_start);
                if (end < p.accrual_end)      // after maturity (the maturity day itself): last outstanding
                    premium_weight_.back() += per_day * static_cast<double>(p.accrual_end - std::max(p.accrual_start, end));
                if (!(lo < hi)) continue;
                for (std::size_t j = index_of(lo); nodes_[j] < hi; ++j) {
                    const double half = 0.5 * per_day * static_cast<double>(nodes_[j + 1] - nodes_[j]);
                    premium_weight_[j] += half;
                    premium_weight_[j + 1] += half;
                }
            }
        }

        [[nodiscard]] std::span<const Core::Date> nodes() const noexcept { return nodes_; }
        [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
        [[nodiscard]] std::span<const double> protection_weights() const noexcept { return protection_weight_; }
        [[nodiscard]] std::span<const double> premium_weights() const noexcept { return premium_weight_; }

    private:
        void build_nodes(const Instruments::Credit::Tranche& tranche, Core::Period step) {
            const Core::Date start = tranche.protection_start;
            const Core::Date end = tranche.maturity;
            const auto add_if_inside = [&](Core::Date d) { if (start < d && d < end) nodes_.push_back(d); };
            nodes_.push_back(start);
            nodes_.push_back(end);
            for (const auto& p : tranche.periods) {
                add_if_inside(p.accrual_start);
                add_if_inside(p.accrual_end);
            }
            for (int k = 1;; ++k) {
                const Core::Date d = Core::add_period(start, step * k);
                if (!(d < end)) break;
                nodes_.push_back(d);
            }
            std::ranges::sort(nodes_);
            const auto duplicates = std::ranges::unique(nodes_);
            nodes_.erase(duplicates.begin(), duplicates.end());
        }

        [[nodiscard]] std::size_t index_of(Core::Date d) const noexcept {
            return static_cast<std::size_t>(std::ranges::lower_bound(nodes_, d) - nodes_.begin());
        }

        std::vector<Core::Date> nodes_;
        std::vector<double> protection_weight_;  // coefficient of E_j in the protection leg
        std::vector<double> premium_weight_;     // coefficient of O_j in the risky annuity
    };

} // namespace Pricing::Credit

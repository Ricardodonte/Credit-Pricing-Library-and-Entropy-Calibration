// Exact integration of the CDS legs (ISDA standard model).
// Between two consecutive nodes of the grid, the discount factor and the survival probability are both
// exponential in time: forwards are piecewise flat (log-linear DFs) and hazard rates piecewise constant,
// provided the grid contains every pillar of both curves. The legs then have closed forms on each segment
// [a, b]. With L = ln(Qa/Qb), X = L + ln(Pa/Pb), phi1(X) = (1 - e^-X)/X, phi2(X) = (1 - e^-X - X e^-X)/X^2:
//     protection:          ∫ P dQ            = Pa Qa L phi1(X)
//     accrued on default:  ∫ tau(t) P dQ     = Pa Qa L [tau_a phi1(X) + (tau_b - tau_a) phi2(X)]
// where tau(t) is the Act/360 accrual since the start of the premium period, linear in t.
// Only P and Q at the nodes are needed, whatever the time axes of the two curves.

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

#include "Core/Dates.h"
#include "Instruments/Credit/Cds.h"

namespace Pricing::Credit {

    namespace detail {
        /// @brief phi1(x) = (1 - e^-x) / x implementation, with its Taylor expansion near 0.
        [[nodiscard]] inline double phi1(double x) noexcept {
            if (std::abs(x) < 1e-3) return 1.0 - x / 2.0 * (1.0 - x / 3.0 * (1.0 - x / 4.0 * (1.0 - x / 5.0)));
            return -std::expm1(-x) / x;
        }
        /// @brief Phi2(x) = (1 - e^-x - x e^-x) / x^2 implementation $
        ///  with its Taylor expansion near 0.
        [[nodiscard]] inline double phi2(double x) noexcept {
            if (std::abs(x) < 1e-3) return 0.5 - x / 3.0 + x * x / 8.0 - x * x * x / 30.0 + x * x * x * x / 144.0;
            return (-std::expm1(-x) - x * std::exp(-x)) / (x * x);
        }
    }

    /// @brief Undiscounted-by-recovery leg values, per unit notional.
    struct CdsLegPv {
        double protection = 0.0;  ///< ∫ P dQ over the protection period (multiply by 1 - R)
        double premium = 0.0;     ///< risky annuity: coupons paid on survival + accrued on default
    };

    /// @brief Integration grid of a CDS: nodes, discount factors and accrual fractions are computed once;
    ///        the legs are then evaluated for any survival function in O(number of nodes).
    ///        This is what makes the bootstrap objective cheap and pure: the credit curve is never touched.
    class CdsIntegrationGrid {
    public:
        /// @param credit_pillars pillar dates of the credit curve (must include every pillar that falls
        ///        inside the protection period for the integration to be exact).
        template <class YieldCurveT>
        CdsIntegrationGrid(const Instruments::Credit::Cds& cds, const YieldCurveT& yield_curve,
                           std::span<const Core::Date> credit_pillars) {
            if (cds.periods.empty())
                throw std::invalid_argument("CdsIntegrationGrid: CDS has no premium period");
            const Core::Date start = cds.protection_start;
            const Core::Date end = cds.maturity;
            if (!(start < end))
                throw std::invalid_argument("CdsIntegrationGrid: protection start must be before maturity");

            const auto add_if_inside = [&](Core::Date d) { if (start < d && d < end) nodes_.push_back(d); };
            nodes_.push_back(start);
            nodes_.push_back(end);
            for (const auto& p : cds.periods) {
                add_if_inside(p.accrual_start);
                add_if_inside(p.accrual_end);
            }
            for (const Core::Date d : yield_curve.pillar_dates()) add_if_inside(d);
            for (const Core::Date d : credit_pillars) add_if_inside(d);
            std::ranges::sort(nodes_);
            const auto duplicates = std::ranges::unique(nodes_);
            nodes_.erase(duplicates.begin(), duplicates.end());

            df_.reserve(nodes_.size());
            for (const Core::Date d : nodes_) df_.push_back(yield_curve.discount(d));

            // Accrual fractions at both ends of each segment, relative to the premium period containing it.
            const std::size_t n_segments = nodes_.size() - 1;
            tau_start_.assign(n_segments, 0.0);
            tau_end_.assign(n_segments, 0.0);
            for (std::size_t j = 0; j < n_segments; ++j) {
                const auto it = std::ranges::upper_bound(cds.periods, nodes_[j], {},
                                                         &Instruments::Credit::CdsPeriod::accrual_start);
                if (it == cds.periods.begin()) continue;  // before the first accrual date: nothing accrued
                const Core::Date accrual_start = std::prev(it)->accrual_start;
                tau_start_[j] = static_cast<double>(nodes_[j] - accrual_start) / 360.0;
                tau_end_[j] = static_cast<double>(nodes_[j + 1] - accrual_start) / 360.0;
            }

            // Coupons paid on survival: survival observed at the end of the accrual period (maturity for the last).
            premiums_.reserve(cds.periods.size());
            for (const auto& p : cds.periods) {
                const Core::Date observation = std::min(p.accrual_end, end);
                if (!(start < observation)) continue;  // period entirely before the protection start
                const auto node = static_cast<std::size_t>(std::ranges::lower_bound(nodes_, observation) - nodes_.begin());
                premiums_.push_back({p.accrual, yield_curve.discount(p.payment), node});
            }
        }

        [[nodiscard]] std::span<const Core::Date> nodes() const noexcept { return nodes_; }
        [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }

        /// @brief Index of the last node on or before d (0 if d is before the first node).
        [[nodiscard]] std::size_t last_node_on_or_before(Core::Date d) const noexcept {
            const auto it = std::ranges::upper_bound(nodes_, d);
            return it == nodes_.begin() ? 0 : static_cast<std::size_t>(it - nodes_.begin()) - 1;
        }

        /// @brief Leg values restricted to nodes [first, last]: segments inside that range, and coupons whose
        ///        survival observation node lies in ]first, last].
        /// @param survival callable node index -> survival probability at that node.
        template <class SurvivalAtNode>
        [[nodiscard]] CdsLegPv accumulate(SurvivalAtNode&& survival, std::size_t first, std::size_t last) const {
            CdsLegPv pv;
            double q_a = survival(first);
            for (std::size_t j = first; j < last; ++j) {
                const double q_b = survival(j + 1);
                if (q_b < q_a) {
                    const double l = std::log(q_a / q_b);
                    const double x = l + std::log(df_[j] / df_[j + 1]);
                    const double w = df_[j] * q_a * l;
                    const double p1 = detail::phi1(x);
                    const double p2 = detail::phi2(x);
                    pv.protection += w * p1;
                    pv.premium += w * (tau_start_[j] * p1 + (tau_end_[j] - tau_start_[j]) * p2);
                }
                q_a = q_b;
            }
            for (const auto& c : premiums_) {
                if (first < c.observation_node && c.observation_node <= last)
                    pv.premium += c.accrual * c.df_payment * survival(c.observation_node);
            }
            return pv;
        }

    private:
        struct PremiumCoupon {
            double accrual;
            double df_payment;
            std::size_t observation_node; // index on the date on which the coupon is paid
        };

        std::vector<Core::Date> nodes_;
        std::vector<double> df_;          // discount factor at each node
        std::vector<double> tau_start_;   // accrual fraction at the start of each segment
        std::vector<double> tau_end_;     // accrual fraction at the end of each segment
        std::vector<PremiumCoupon> premiums_;
    };

} // namespace Pricing::Credit

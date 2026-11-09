// Tranche pricer: a tranche prepared once for a yield curve, a reference pool and a loss model, then valued
// for any correlation (base correlation curve, pair of base correlations, or compound correlation).
// Base correlation decomposition. With EL(K, rho)(t) = E[min(L_t, K)] under the flat correlation rho,
// the tranche [K1, K2] has expected loss (EL(K2, rho2) - EL(K1, rho1)) / (K2 - K1), with rho1 = rho(K1) and
// rho2 = rho(K2) read on the base correlation curve. Both legs being linear in the expected loss, the tranche
// legs are the same combination of the legs of the base tranches [0, K1] and [0, K2] (BaseTrancheLegs).
// What is computed once, at construction (principle 3 of the guide):
//   - the integration grid and its leg weights (contract + yield curve);
//   - the loss model state at every node (marginal default probabilities, Gaussian thresholds...).
// Valuing then costs one expected_tranche_loss per node and base tranche; nothing is allocated by the pricer.
// Neither the curves nor the pool are kept: if the yield or credit curves change, prepare the tranche again.
// Not modelled (standard simplifications of the base correlation framework): recovery amortisation of the
// most senior tranche, names already defaulted in the index (index factor), stochastic recoveries.

#pragma once
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Core/Dates.h"
#include "Instruments/Credit/Tranche.h"
#include "Models/Credit/LossModel.h"
#include "Pricing/Credit/ReferencePool.h"
#include "Pricing/Credit/TrancheIntegrationGrid.h"
#include "Pricing/Credit/UpfrontValuation.h"

namespace Pricing::Credit {

    /// @brief Valuation of a tranche as of its trade date, per unit tranche notional (protection buyer).
    struct TrancheValuation : UpfrontValuation {};

    /// @brief Legs of the base tranche [0, K], in index-notional units (not normalised by K).
    struct BaseTrancheLegs {
        double protection = 0.0;  ///< PV of E[min(L, K)] increments
        double annuity = 0.0;     ///< risky annuity on the outstanding K - E[min(L, K)]
    };

    /// @brief Anything that gives a base correlation for a detachment point.
    template <class C>
    concept CorrelationCurve = requires(const C& curve, double detachment) {
        { curve.correlation(detachment) } -> std::convertible_to<double>;
    };

    template <Models::Credit::TrancheLossModel LossModel>
    class PreparedTranche {
    public:
        using loss_model_type = LossModel;

        template <class YieldCurveT, class CreditCurveT>
        PreparedTranche(Instruments::Credit::Tranche tranche, const YieldCurveT& yield_curve,
                        const ReferencePool<CreditCurveT>& pool, LossModel model = {},
                        const TrancheGridSettings& settings = {})
            : tranche_(std::move(tranche)), model_(std::move(model)), grid_(tranche_, yield_curve, settings),
              df_cash_settlement_(yield_curve.discount(tranche_.cash_settlement_date)) {
            if (yield_curve.reference_date() != tranche_.trade_date || !pool.all_curves_as_of(tranche_.trade_date))
                throw std::invalid_argument("PreparedTranche: all curves must be built as of the tranche trade date");

            std::vector<double> default_probability(pool.size());
            states_.reserve(grid_.size());
            for (const Core::Date d : grid_.nodes()) {
                pool.default_probabilities(d, default_probability);
                states_.push_back(model_.prepare({default_probability, pool.loss_given_default()}));
            }
        }

        [[nodiscard]] const Instruments::Credit::Tranche& contract() const noexcept { return tranche_; }
        [[nodiscard]] const LossModel& loss_model() const noexcept { return model_; }
        [[nodiscard]] const TrancheIntegrationGrid& grid() const noexcept { return grid_; }

        /// @brief Legs of the base tranche [0, strike] under the flat correlation rho.
        [[nodiscard]] BaseTrancheLegs base_legs(double strike, double rho) const {
            BaseTrancheLegs legs;
            if (strike <= 0.0) return legs;
            const auto protection_weight = grid_.protection_weights();
            const auto premium_weight = grid_.premium_weights();
            for (std::size_t j = 0; j < states_.size(); ++j) {
                const double expected_loss = model_.expected_tranche_loss(states_[j], strike, rho);
                legs.protection += protection_weight[j] * expected_loss;
                legs.annuity += premium_weight[j] * (strike - expected_loss);
            }
            return legs;
        }

        /// @brief Tranche valuation from the legs of its two base tranches [0, K1] and [0, K2].
        [[nodiscard]] TrancheValuation valuation(const BaseTrancheLegs& attachment_legs,
                                                 const BaseTrancheLegs& detachment_legs) const noexcept {
            const double width = tranche_.width();
            return {{(detachment_legs.protection - attachment_legs.protection) / width,
                     (detachment_legs.annuity - attachment_legs.annuity) / width, tranche_.accrued_at_step_in(),
                     df_cash_settlement_, tranche_.running_coupon, tranche_.notional}};
        }

        /// @brief Base correlation pricing: rho1 at the attachment, rho2 at the detachment.
        [[nodiscard]] TrancheValuation value(double rho_attachment, double rho_detachment) const {
            return valuation(base_legs(tranche_.attachment, rho_attachment),
                             base_legs(tranche_.detachment, rho_detachment));
        }

        /// @brief Compound correlation pricing: the same correlation for both base tranches.
        [[nodiscard]] TrancheValuation value(double compound_correlation) const {
            return value(compound_correlation, compound_correlation);
        }

        /// @brief Base correlation pricing, correlations read on the curve.
        template <CorrelationCurve Curve>
        [[nodiscard]] TrancheValuation value(const Curve& base_correlation) const {
            const double rho_attachment = tranche_.is_equity() ? 0.0 : base_correlation.correlation(tranche_.attachment);
            return value(rho_attachment, base_correlation.correlation(tranche_.detachment));
        }

    private:
        Instruments::Credit::Tranche tranche_;
        LossModel model_;
        TrancheIntegrationGrid grid_;
        double df_cash_settlement_;
        std::vector<typename LossModel::state_type> states_;  // model state at each grid node
    };

} // namespace Pricing::Credit

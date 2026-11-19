//
// Created by ricar on 06/05/2026.
// Adjusted Binomial Pricer - Binomial with Variance Matching via Mass Transfer
//

#pragma once
#include "base_tranche_pricer.h"
#include "Market/CreditCurve.h"
#include <vector>

namespace Pricer {

    class AdjustedBinomialPricer final : public BaseTranchePricer {

    public:

        explicit AdjustedBinomialPricer(
            const std::vector<Market::CreditCurve>& credit_curves,
            int   n_credits,
            double recovery_rate
        );

        explicit AdjustedBinomialPricer(
            const std::vector<Market::CreditCurve>& credit_curves,
            int   n_credits,
            const std::vector<double>& recovery_rates
        );

        [[nodiscard]] double expected_min_loss(double K, double t, double rho) const override;

    private:

        struct BinomialParams {
            double p;
            double loss_avg;
            double sigma2_A;
            double sigma2_E;
            double m;
        };

        struct MassTransfer {
            double alpha;
            double eps_q;
            double eps_q_plus1;
        };

        const std::vector<Market::CreditCurve>& m_credit_curves;
        std::vector<double> m_recovery_rates;
        std::vector<double> m_lgd_values;
        int                 m_n_credits;

        mutable std::vector<double> m_C_thresh;
        mutable double              m_cached_t{ -1.0 };


        void update_thresholds(double t) const;

        [[nodiscard]] static inline double cond_pd(
            double C_i,
            double Z,
            double sqrt_rho,
            double sqrt_1_minus_rho
        ) noexcept;

        [[nodiscard]] BinomialParams compute_binomial_params(
            double rho, double Z
        ) const;

        [[nodiscard]] static MassTransfer compute_mass_transfer(
            const BinomialParams& params,
            int N
        ) noexcept;

        [[nodiscard]] double compute_expected_tranche_loss(
            double K,
            const BinomialParams&  params,
            const MassTransfer&    transfer
        ) const;
    };

} // namespace Pricer
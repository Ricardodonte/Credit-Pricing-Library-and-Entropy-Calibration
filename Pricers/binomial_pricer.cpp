//
// Created by ricar on 06/05/2026.
// Implementation of Adjusted Binomial Pricer
//

#include "Pricers/binomial_pricer.h"
#include "Core/numericals.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace Pricer {

AdjustedBinomialPricer::AdjustedBinomialPricer(
    const std::vector<Market::CreditCurve>& credit_curves,
    const int   n_credits,
    const double recovery_rate
)
    : m_credit_curves(credit_curves)
    , m_n_credits(n_credits)
{
    if (n_credits <= 0)
        throw std::invalid_argument("AdjustedBinomialPricer: n_credits must be > 0");
    if (recovery_rate < 0.0 || recovery_rate > 1.0)
        throw std::invalid_argument("AdjustedBinomialPricer: recovery_rate must be in [0, 1]");
    if (static_cast<int>(credit_curves.size()) != n_credits)
        throw std::invalid_argument("AdjustedBinomialPricer: credit_curves.size() != n_credits");

    m_recovery_rates.assign(n_credits, recovery_rate);
    m_lgd_values.assign(n_credits, 1.0 - recovery_rate);
    m_C_thresh.resize(n_credits);
}

AdjustedBinomialPricer::AdjustedBinomialPricer(
    const std::vector<Market::CreditCurve>& credit_curves,
    const int   n_credits,
    const std::vector<double>& recovery_rates
)
    : m_credit_curves(credit_curves)
    , m_recovery_rates(recovery_rates)
    , m_n_credits(n_credits)
{
    if (n_credits <= 0)
        throw std::invalid_argument("AdjustedBinomialPricer: n_credits must be > 0");
    if (static_cast<int>(recovery_rates.size()) != n_credits)
        throw std::invalid_argument("AdjustedBinomialPricer: recovery_rates.size() != n_credits");
    if (static_cast<int>(credit_curves.size()) != n_credits)
        throw std::invalid_argument("AdjustedBinomialPricer: credit_curves.size() != n_credits");

    for (const double rr : recovery_rates)
        if (rr < 0.0 || rr > 1.0)
            throw std::invalid_argument("AdjustedBinomialPricer: all recovery_rates must be in [0, 1]");

    m_lgd_values.resize(n_credits);
    for (int i = 0; i < n_credits; ++i)
        m_lgd_values[i] = 1.0 - recovery_rates[i];

    m_C_thresh.resize(n_credits);
}

void AdjustedBinomialPricer::update_thresholds(const double t) const
{
    if (t == m_cached_t) return;

    for (int i = 0; i < m_n_credits; ++i) {
        const double sp = m_credit_curves[static_cast<std::size_t>(i)].survival_probability(t);
        if      (const double pd = 1.0 - sp; pd <= 1e-12)      m_C_thresh[i] = -10.0;
        else if (pd >= 1.0-1e-12)  m_C_thresh[i] =  10.0;
        else                       m_C_thresh[i] = Core::norm_inv(pd);
    }

    m_cached_t = t;
}

inline double AdjustedBinomialPricer::cond_pd(
    const double C_i,
    const double Z,
    const double sqrt_rho,
    const double sqrt_1_minus_rho
) noexcept
{
    return Core::norm_cdf((C_i - sqrt_rho * Z) / sqrt_1_minus_rho);
}


AdjustedBinomialPricer::BinomialParams AdjustedBinomialPricer::compute_binomial_params(
    const double rho,const double Z ) const
{
    const double sqrt_rho          = std::sqrt(rho);
    const double sqrt_1_minus_rho  = std::sqrt(std::max(1.0 - rho, 0.0));
    const auto Nd                = static_cast<double>(m_n_credits);

    const double total_lgd = std::accumulate(m_lgd_values.begin(), m_lgd_values.end(), 0.0);
    const double loss_avg  = total_lgd / (Nd * Nd);

    double sum_lgd_pd   = 0.0;
    double sum_lgd2_var = 0.0;

    for (int i = 0; i < m_n_credits; ++i) {
        const double pd  = cond_pd(m_C_thresh[i], Z, sqrt_rho, sqrt_1_minus_rho);
        const double lgd = m_lgd_values[i];
        sum_lgd_pd   += lgd * pd;
        sum_lgd2_var += lgd * lgd * pd * (1.0 - pd);
    }

    const double p = sum_lgd_pd / total_lgd;
    const double m = Nd * p;
    const double sigma2_A_x = Nd * p * (1.0 - p);
    const double sigma2_E_x = sum_lgd2_var * Nd * Nd / (total_lgd * total_lgd);
    return BinomialParams{ p, loss_avg, sigma2_A_x, sigma2_E_x, m };
}


AdjustedBinomialPricer::MassTransfer
AdjustedBinomialPricer::compute_mass_transfer(const BinomialParams& params,const int N) noexcept
{
    const double sigma2_A_x = params.sigma2_A;
    const double sigma2_E_x = params.sigma2_E;

    if (std::abs(sigma2_A_x - sigma2_E_x) < 1e-14 || sigma2_A_x < 1e-14)
        return MassTransfer{ 1.0, 0.0, 0.0 };

    const double m   = params.m;
    const auto Nd  = static_cast<double>(N);
    const double q   = std::floor(m);
    const double q1  = q + 1.0;

    const double d_lo = q  - m;
    const double d_hi = q1 - m;
    const double common = d_hi * d_hi + (d_lo * d_lo - d_hi * d_hi) * d_hi;

    const double denom = sigma2_A_x * Nd - common;
    if (std::abs(denom) < 1e-14)
        return MassTransfer{ 1.0, 0.0, 0.0 };

    const double alpha        = (sigma2_E_x * Nd - common) / denom;
    const double eps_q        = (1.0 - alpha) * d_hi;
    const double eps_q_plus1  = (1.0 - alpha) - eps_q;

    return MassTransfer{ alpha, eps_q, eps_q_plus1 };
}


double AdjustedBinomialPricer::compute_expected_tranche_loss(const double K,const BinomialParams&  params,const MassTransfer&    transfer) const
{
    const int    N        = m_n_credits;
    const double p        = params.p;
    const double loss_avg = params.loss_avg;
    const double max_loss = static_cast<double>(N) * loss_avg;

    constexpr double P_EPS = 1e-12;
    if (p <= P_EPS)       return 0.0;
    if (p >= 1.0 - P_EPS) return std::min(max_loss, K);

    if (K >= max_loss - 1e-14)
        return max_loss * p;

    const double q_val = 1.0 - p;

    const int q_idx  = static_cast<int>(std::floor(params.m));
    const int q1_idx = q_idx + 1;

    const int k_max = (loss_avg > 0.0)
        ? std::min(static_cast<int>(std::ceil(K / loss_avg)), N)
        : N;

    double f_raw = std::pow(q_val, static_cast<double>(N));

    const double f0_adj = transfer.alpha * f_raw
              + (q_idx == 0) * transfer.eps_q
              + (q1_idx == 0) * transfer.eps_q_plus1;

    double E_loss     = 0.0;
    double prob_below = f0_adj;


    for (int k = 1; k <= k_max; ++k) {

        f_raw *= (p / q_val) * static_cast<double>(N - k + 1) / static_cast<double>(k);

        double f_adj = transfer.alpha * f_raw;
        f_adj += (k == q_idx) * transfer.eps_q + (k == q1_idx) * transfer.eps_q_plus1;

        const double loss_k = static_cast<double>(k) * loss_avg;

        if (loss_k >= K) {
            const double tail = 1.0 - prob_below;
            E_loss += K * tail;
            return std::max(E_loss, 0.0);
        }

        E_loss    += loss_k * f_adj;
        prob_below += f_adj;
    }

    E_loss += K * (1.0 - prob_below);
    return std::max(E_loss, 0.0);
}

double AdjustedBinomialPricer::expected_min_loss(const double K,const double t,const double rho) const
{
    if (K <= 0.0) return 0.0;

    update_thresholds(t);

    double E_min = 0.0;

    for (std::size_t gi = 0; gi < static_cast<std::size_t>(Core::N_GH); ++gi) {
        const double Z = std::sqrt(2.0) * Core::GH_NODES[gi];
        const double w = Core::GH_WEIGHTS[gi] / std::sqrt(MathConstants::PI);

        const BinomialParams  params   = compute_binomial_params(rho, Z);
        const MassTransfer    transfer = compute_mass_transfer(params, m_n_credits);

        E_min += w * compute_expected_tranche_loss(K, params, transfer);
    }

    return std::max(E_min, 0.0);
}

}
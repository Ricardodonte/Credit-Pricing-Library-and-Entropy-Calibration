// Contracts of the portfolio loss models used to price tranches.
// Everything a tranche price needs from a model is the expected base-tranche loss
//     E[min(L_t, K)]       L_t = sum_i l_i 1{tau_i <= t},   l_i = w_i (1 - R_i)   (fraction of index notional)
// for a strike K and a correlation rho: a tranche [K1, K2] is the difference of two base tranches. Models are
// split in two levels, so that each piece can be replaced on its own (open-closed):
//  1. TrancheLossModel: the full model, E[min(L_t, K)] from the marginal default probabilities of the names.
//     A model is used in two steps:
//        prepare(pool)                     -> state   everything that depends on the pool at date t only
//                                                     (Gaussian thresholds, averages...): done once per date;
//        expected_tranche_loss(state, K, rho)         pure and cheap: called by the pricer and the solver.
//     This is principle 3 of the guide applied to tranches: in the base correlation bootstrap, only rho moves.
//  2. ConditionalLossModel: inside a one-factor copula, E[min(L, K) | Z] from the conditional default
//     probabilities p_i(Z). This is where the classical approximations differ (normal, adjusted binomial, exact
//     recursion), independently of the copula.

#pragma once
#include <concepts>
#include <span>

namespace Models::Credit {

    /// @brief Marginal state of the reference pool at one date.
    struct PoolSnapshot {
        std::span<const double> default_probability;  ///< P(tau_i <= t), one per name
        std::span<const double> loss_given_default;   ///< l_i = w_i (1 - R_i), fraction of the index notional
    };

    template <class M>
    concept TrancheLossModel = requires(const M& model, const PoolSnapshot& pool,
                                        const typename M::state_type& state, double strike, double rho) {
        typename M::state_type;
        { model.prepare(pool) } -> std::same_as<typename M::state_type>;
        { model.expected_tranche_loss(state, strike, rho) } -> std::convertible_to<double>;
    };

    template <class C>
    concept ConditionalLossModel = requires(const C& model, std::span<const double> conditional_pd,
                                            std::span<const double> loss_given_default, double strike) {
        { model.expected_tranche_loss(conditional_pd, loss_given_default, strike) } -> std::convertible_to<double>;
    };

} // namespace Models::Credit

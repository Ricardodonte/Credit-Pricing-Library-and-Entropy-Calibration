//
// Created by ricar on 17/04/2026.
//

#pragma once
#include "base_tranche_pricer.h"
#include "Market/CreditCurve.h"


namespace  Pricer {

    class GaussianPricer final : public BaseTranchePricer  {

    public :

        explicit GaussianPricer(const std::vector<Market::CreditCurve> & credit_curves , const int n_credits , std::vector<double> & recovery_rates  ) :
        m_credit_curves(credit_curves) , m_recovery_rates(recovery_rates) , m_credits(n_credits) {};

        [[nodiscard]] double expected_min_loss(double K, double t, double rho) const override;

    private :

        const std::vector<Market::CreditCurve> &m_credit_curves ;
        const std::vector<double> &m_recovery_rates ;
        const int m_credits ;

        [[nodiscard]] inline std::vector<double> default_probability(const std::vector<Market::CreditCurve> &q , double t, double Z, double rho) const;
        [[nodiscard]] inline std::pair<double, double> mean_var(const std::vector<double> &pd) const ;
    };


}

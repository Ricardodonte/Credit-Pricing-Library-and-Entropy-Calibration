//
// Created by ricar on 16/04/2026.
//

#pragma once
#include "../Base/base_tranche_pricer.h"
#include "../../../Market/Curves/CDS/CreditCurve.h"

namespace Pricer {

    class RecursionPricer final : public BaseTranchePricer {

    public:

        explicit RecursionPricer(const std::vector<Market::CreditCurve>& credit_curves , const int n_credit , const double recovery_rate = 0.40) :
        m_credit_curves(credit_curves), m_n_credit(n_credit), m_recovery_rate(recovery_rate) ,m_unit( (1.0 - recovery_rate) / n_credit ) {

            if (n_credit <=0)
                throw std::invalid_argument("n_credit must be greater than 0");
            if (recovery_rate <0 || recovery_rate > 1)
                throw std::invalid_argument("recovery_rate must be greater than 0 and lower than 1");
            if (credit_curves.empty() || credit_curves.size() != n_credit)
                throw std::invalid_argument("credit_curves must have the same size with the number of credits ");
        }

        [[nodiscard]] double expected_min_loss(double K, double t, double rho) const override ;

    private :
        std::vector<Market::CreditCurve> m_credit_curves ;
        const int m_n_credit ;
        const double m_recovery_rate ;
        const double m_unit ;

        static inline double cond_pd(double C_t , double Z , double rho) noexcept ;


    };
}
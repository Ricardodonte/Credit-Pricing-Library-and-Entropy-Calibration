//
// Created by ricar on 10/04/2026.
//

#include "cds_pricers.h"

namespace Pricer {
    double CDSPricer::rpv01(const Core::Date valuation_date) const {

        if ( valuation_date == m_cds.EffectiveDate ) {

            double rpv01 = 0.0;
            const auto &it = m_grid.premiumTimes.begin();

            for (auto kt = std::next(it); kt != m_grid.premiumTimes.end(); ++kt) {
                const Core::Date& curr = *kt;
                const Core::Date& prev = *std::prev(kt);
                const double t_curr = Core::year_fraction(valuation_date, curr, m_cds.conventionalDayCount);
                const double t_prev = Core::year_fraction(valuation_date, prev, m_cds.conventionalDayCount);
                const double tau    = Core::year_fraction(prev, curr, m_cds.conventionalDayCount);
                rpv01 += tau * m_y_curve.discount(t_curr) *
                    (m_credit_curve.survival_probability(t_prev) + m_credit_curve.survival_probability(t_curr));
            }

            return 0.5 * rpv01;
        }

        const auto it = std::ranges::upper_bound(m_grid.premiumTimes
                                                 ,valuation_date,
                                                 [](const Core::Date& a, const Core::Date& b) {
                                                     return a.getJulianDays() < b.getJulianDays();
                                                 });


        if ( (it != m_grid.premiumTimes.end()) & (it != m_grid.premiumTimes.begin()) ) {

            const Core::Date& next_date = *it;
            const Core::Date& prev_date = *std::prev(it);

            const double tau_curr_prev = Core::year_fraction(prev_date,valuation_date,m_cds.conventionalDayCount);
            const double tau_next_curr = Core::year_fraction(valuation_date,next_date,m_cds.conventionalDayCount);

            const double prv01_1 = tau_curr_prev * m_y_curve.discount(tau_next_curr) * (1 -
                m_credit_curve.survival_probability(tau_next_curr));
            const double prv01_2 = 0.5 * tau_next_curr * m_y_curve.discount(tau_next_curr) * (1 -
                m_credit_curve.survival_probability(tau_next_curr));
            const double prv01_3 = Core::year_fraction(prev_date,next_date,m_cds.conventionalDayCount) *
                m_y_curve.discount(tau_next_curr) * m_credit_curve.survival_probability(tau_next_curr) ;

            double prv01_4 = 0.0;
            auto kt = it;
            auto next_kt = std::next(kt);

            while (next_kt != m_grid.premiumTimes.end()) {
                const Core::Date& Date_0 = *kt;
                const Core::Date& Date_1 = *next_kt;
                const double d1 = Core::year_fraction(valuation_date , Date_1 , m_cds.conventionalDayCount);
                const double d0 = Core::year_fraction(valuation_date , Date_0 , m_cds.conventionalDayCount);

                prv01_4 +=  Core::year_fraction(Date_0,Date_1,m_cds.conventionalDayCount) *
                    m_y_curve.discount(d1) * ( m_credit_curve.survival_probability(d0) + m_credit_curve.survival_probability(d1) );

                ++kt;
                ++next_kt;
            }
            return prv01_1 + prv01_2 + prv01_3 + 0.5*prv01_4;
        }
        throw std::runtime_error("Valuation date greater than maturity");
    }


    double CDSPricer::default_leg(const Core::Date valuation_date) const {

        const auto it = std::ranges::upper_bound(m_grid.defaultTimes
                                                ,valuation_date,
                                                [](const Core::Date& a, const Core::Date& b) {
                                                    return a.getJulianDays() < b.getJulianDays();
                                                });

        if (it != m_grid.defaultTimes.end()) {

            double defLeg = 0.0;
            const Core::Date& next_date = *it;

            constexpr double t0 = 0 ;
            const double t1 = Core::year_fraction(valuation_date,next_date,m_cds.conventionalDayCount);

            defLeg = (m_credit_curve.survival_probability(t0) - m_credit_curve.survival_probability(t1)) *
                (m_y_curve.discount(t0) + m_y_curve.discount(t1)) ;

            for (auto kt = std::next(it); kt != m_grid.defaultTimes.end(); ++kt) {

                const Core::Date& Date_0 = *std::prev(kt);
                const Core::Date& Date_1 = *kt;

                const double d1 = Core::year_fraction(valuation_date , Date_1 , m_cds.conventionalDayCount);
                const double d0 = Core::year_fraction(valuation_date , Date_0 , m_cds.conventionalDayCount);

                defLeg += (m_credit_curve.survival_probability(d0) - m_credit_curve.survival_probability(d1)) *
                    (m_y_curve.discount(d0) + m_y_curve.discount(d1));

            }

            return defLeg * 0.5 * (1-m_cds.RecoveryRate);

        }

        throw std::runtime_error("Valuation date greater than maturity or lower than effective date");

    }


    double CDSPricer::par_spread() const {
            return default_leg(m_cds.ValuationDate) / rpv01(m_cds.ValuationDate);
    }

    double CDSPricer::upfront() const {

        if (m_cds.FixedSpread.has_value()) {
            const double up = ( m_cds.FixedSpread.value() - par_spread() ) * rpv01(m_cds.ValuationDate);
            return up ;
        }

        throw std::runtime_error("CDS must have fixed spread in order to determine upfront payment");

    }

    double CDSPricer::npv() const {

        if (m_cds.FixedSpread.has_value())
        {
            const double npv = (m_cds.FixedSpread.value() - par_spread()) * rpv01(m_cds.ValuationDate) ;
             return npv * m_cds.Nominal ;
        }

        if (m_cds.ContractualSpread.has_value())
        {
            const double npv = (m_cds.ContractualSpread.value() - par_spread())* rpv01(m_cds.ValuationDate) ;
            return npv * m_cds.Nominal ;
        }

        throw std::runtime_error("CDS must have either ContractualSpread or both FixedSpread");
    }

}

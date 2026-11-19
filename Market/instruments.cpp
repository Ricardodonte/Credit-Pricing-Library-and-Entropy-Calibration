//
// Created by ricar on 10/04/2026.
//

#include "instruments.h"
#include "YieldCurve.h"
#include <cmath>

namespace Market {

double Deposit::implied_df() const {

    const double df = 1/(1+ maturity*rate);
    return df;
}

double Future::fra_market_rate() const {
        const double future_rate = (100.0 - price) / 100.0;
        const double conv_adj    = 0.5 * volatility * volatility * effective_date * maturity_date;
        return future_rate - conv_adj;
}

double Future::npv(const YieldCurve &y_curve) const {

    const double fra_rate   = fra_market_rate();
    const double implied_fra_rate = y_curve.forward_rate(effective_date,maturity_date);
    return implied_fra_rate - fra_rate;
}

Swap::Schedule Swap::buildSchedule() const {

    Schedule s;

    const int n_fixed = static_cast<int>(std::round(maturity / fixedLeg_freq));
    for (int i = 0; i <= n_fixed; ++i) {
        double t = i * fixedLeg_freq;
        s.fixed.push_back(t);
    }

    const int n_floating = static_cast<int>(std::round(maturity / floatLeg_freq));
    for (int i = 0; i <= n_floating; ++i) {
        double t = i * floatLeg_freq;
        s.floating.push_back(t);
    }

    return s;
}

    double Swap::npv(const YieldCurve &y_curve, const Schedule &s) const {

    double pv_01 = 0.0;

    for (size_t i = 1; i < s.fixed.size(); ++i) {
        pv_01 += fixedLeg_freq * y_curve.discount(s.fixed[i]);
    }

    const double pv_floating =
        y_curve.discount(s.floating.front()) - y_curve.discount(s.floating.back());

    return fixedRate * pv_01 - pv_floating;
}


    int CDS::get_frequency() const {

        switch(frequency) {
            case Core::Frequency::ANNUAL:      return 12;
            case Core::Frequency::SEMI_ANNUAL: return 6;
            case Core::Frequency::QUARTERLY:   return 3;
            default:
                throw std::runtime_error("Unknown frequency");
        }

    }

    Core::Date CDS::get_MaturityDate() const {
        const double n_month = maturity * 12;
        return EffectiveDate.add_months( n_month );
    }

    CDS::CDSGrids CDS::buildCDSGrids(const int default_freq) const {

    CDSGrids grid;
    const Core::Date matDate     = get_MaturityDate();
    const int prem_months = get_frequency();
    const int        n_def       = static_cast<int>( std::round(maturity*12 / default_freq) ) ;
    const int        n_prem      = static_cast<int>( std::round(maturity*12 / prem_months) );


    grid.defaultTimes.push_back(EffectiveDate);
    for (int t = 1; t <= n_def; ++t) {
        Core::Date d = EffectiveDate.add_months(t * default_freq);
        grid.defaultTimes.push_back(t == n_def ? matDate : d);
    }

    grid.premiumTimes.push_back(EffectiveDate);
    for (int t = 1; t <= n_prem; ++t) {
        Core::Date d = EffectiveDate.add_months(t * prem_months );
        grid.premiumTimes.push_back(t == n_prem ? matDate : d);
    }

    return grid;
}

}
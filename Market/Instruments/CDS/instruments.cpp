//
// Created by ricar on 10/04/2026.
//

#include "instruments.h"
#include "../../Curves/YieldCurve/YieldCurve.h"
#include <cmath>

namespace Market {

double Deposit::implied_df() const {

    const double df = 1/(1+ maturity*rate);
    return df;
}

double Future::fra_market_rate() const {

    if (volatility.has_value() && future_price.has_value())
    {
        const double future_rate = (100.0 - future_price.value()) / 100.0;
        const double conv_adj    = 0.5 * volatility.value() * volatility.value() * effective_date * maturity_date;
        return future_rate - conv_adj;
    }

    if (fra_rate.has_value())
        return fra_rate.value();

    throw std::runtime_error("Future market rate not available: missing both price/volatility and fra_rate");

}

double Future::npv(const YieldCurve &y_curve) const {

    const double fra   = fra_market_rate();
    const double implied_fra_rate = y_curve.forward_rate(effective_date,maturity_date);
    return implied_fra_rate - fra;
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



   CDS::CDSGrids CDS::buildCDSGrids(const int default_freq) const {
    CDSGrids grid;
    const Core::Date& matDate = MaturityDate;


    grid.defaultTimes.push_back(ValuationDate);

    Core::Date current_def_date = ValuationDate;
    while (true) {
        current_def_date = current_def_date.add_months(default_freq);

        if (current_def_date >= matDate) {
            grid.defaultTimes.push_back(matDate);
            break;
        }
        grid.defaultTimes.push_back(current_def_date);
    }


    grid.premiumTimes.push_back(ValuationDate);

    Core::Date current_imm = CDS::firstIMMOnOrAfter(ValuationDate);

    while (current_imm < matDate) {
        grid.premiumTimes.push_back(current_imm);

        // Move to next IMM date (3 months later)
        auto [imm_year, imm_month, imm_day] = current_imm.toGregorian();
        imm_month += 3;
        if (imm_month > 12) {
            imm_month -= 12;
            imm_year++;
        }
        current_imm = Core::Date(imm_year, imm_month, 20);
    }

    // Ensure MaturityDate is the final coupon date
    if (grid.premiumTimes.empty() || grid.premiumTimes.back() != matDate) {
        grid.premiumTimes.push_back(matDate);
    }

    return grid;
}

    double CDS::get_maturity() const
    {
        return Core::year_fraction(EffectiveDate, MaturityDate, conventionalDayCount);
    }


}

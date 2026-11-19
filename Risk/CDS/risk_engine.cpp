//
// Created by ricar on 11/05/2026.
//

#include "risk_engine.h"
#include "Pricers/CDS/cds_pricers.h"
#include <cmath>
#include <algorithm>

namespace Risk {

    CDSRiskResults CDSRiskEngine::CDSGreeks(const Market::CDS& cds, const CDSBumpConfig &config) const {

         CDSRiskResults results{};

         Pricer::CDSPricer pricer(cds, m_yieldCurve, m_creditCurve);
         results.MtM_value = pricer.npv();

         // ═══════════════════════════════════════════════════════════════════════════════
         // Credit Spread Greeks (CS01 / Credit DV01 and Gamma)
         // ═══════════════════════════════════════════════════════════════════════════════
         if (config.compute_CreditDV01 || config.compute_SpreadGamma) {
             const auto cc_up = build_CreditCurve_Parallel_Shift(+config.h_spread);
             Pricer::CDSPricer pricer_up(cds, m_yieldCurve, cc_up);
             const double npv_up_credit = pricer_up.npv();

             if (config.compute_CreditDV01) {
                 double npv_dn_credit = results.MtM_value;

                 if (config.use_central_diff) {
                     const auto cc_dn = build_CreditCurve_Parallel_Shift(-config.h_spread);
                     Pricer::CDSPricer pricer_dn(cds, m_yieldCurve, cc_dn);
                     npv_dn_credit = pricer_dn.npv();
                 }

                 // CS01 = Price change per 1bp of spread (absolute price variation, not per unit)
                 // With central difference: npv_up - npv_dn = price change for ±1bp total
                 // Divide by 2 (not by h_spread) to get variation for 1bp
                 const double scale = config.use_central_diff ? 2.0 : 1.0;
                 results.Credit_DV01 = (npv_up_credit - npv_dn_credit) / scale;
             }

             if (config.compute_SpreadGamma) {
                 const auto cc_dn = build_CreditCurve_Parallel_Shift(-config.h_spread);
                 Pricer::CDSPricer pricer_dn(cds, m_yieldCurve, cc_dn);
                 const double npv_dn_credit = pricer_dn.npv();

                 // Gamma = d(CS01)/d(spread) = second derivative of price wrt spread
                 // (P_up - 2*P + P_dn) / h gives the rate of change of the first derivative
                 results.SpreadGamma = (npv_up_credit - 2.0 * results.MtM_value + npv_dn_credit) /
                     config.h_spread;
             }
         }

         // ═══════════════════════════════════════════════════════════════════════════════
         // Interest Rate Greeks (DV01 for rates)
         // ═══════════════════════════════════════════════════════════════════════════════
         if (config.compute_YieldDV01) {
             const auto yc_up = build_YieldCurve_Parallel_Shift(+config.h_rate);
             Pricer::CDSPricer pricer_yc_up(cds, yc_up, m_creditCurve);
             const double npv_up_rate = pricer_yc_up.npv();

             double npv_dn_rate = results.MtM_value;

             if (config.use_central_diff) {
                 const auto yc_dn = build_YieldCurve_Parallel_Shift(-config.h_rate);
                 Pricer::CDSPricer pricer_yc_dn(cds, yc_dn, m_creditCurve);
                 npv_dn_rate = pricer_yc_dn.npv();
             }

             // DV01 = Price change per 1bp of rates (absolute price variation)
             const double scale = config.use_central_diff ? 2.0 : 1.0;
             results.InterestRate_DV01 = (npv_up_rate - npv_dn_rate) / scale;
         }

         // ═══════════════════════════════════════════════════════════════════════════════
         // Time Decay (Theta) - Price change per 1 day
         // ═══════════════════════════════════════════════════════════════════════════════
         if (config.compute_Theta) {
             Market::CDS cds_TimeShift = cds;
             cds_TimeShift.MaturityDate = cds_TimeShift.MaturityDate.remove_days(1);
             Pricer::CDSPricer pricer_1d(cds_TimeShift, m_yieldCurve, m_creditCurve);
             results.Theta = pricer_1d.npv() - results.MtM_value;
         }

         return results;
     }

    Market::CreditCurve CDSRiskEngine::build_CreditCurve_Parallel_Shift(const double h_decimal) const {
        Market::CDSMarketData md_bumped = m_mdata;
        for (auto& quote : md_bumped.quotes) {
            quote.value = std::max(quote.value + h_decimal, 1e-4);
        }
        const Market::CreditBoot boot(md_bumped, m_yieldCurve, m_ref_date);
        return boot.curve();
    }

    Market::YieldCurve CDSRiskEngine::build_YieldCurve_Parallel_Shift(const double h_decimal) const {

        Market::YieldCurve bumped_yc(m_ref_date);
        for (const auto& pillar : m_yieldCurve.pillars()) {
            const double t = pillar.time;
            const double df_shifted = pillar.value * std::exp(-h_decimal * t);
            bumped_yc.add_pillar(t, df_shifted);
        }
        return bumped_yc;
    }

    Market::CreditCurve CDSRiskEngine::build_Bucket_CreditCurve_Shift(const double h_decimal, const std::size_t pillar_idx) const {

        Market::CDSMarketData md_bumped = m_mdata;
        if (pillar_idx >= md_bumped.quotes.size()) {
            throw std::out_of_range("CDSRiskEngine: Pillar index out of range");
        }
        md_bumped.quotes[pillar_idx].value =
            std::max(md_bumped.quotes[pillar_idx].value + h_decimal, 1e-4);

        const Market::CreditBoot boot(md_bumped, m_yieldCurve, m_ref_date);
        return boot.curve();
    }

}

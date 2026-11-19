//
// Created by ricar on 10/04/2026.
//

#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cmath>

#include "Core/Dates.h"
#include "Core/types.h"


namespace Market {

    class YieldCurve;

    struct Deposit {
        double maturity;
        double rate;
        [[nodiscard]] double implied_df() const;
    };

    struct Future {
        double effective_date{};
        double maturity_date{};
        std::optional<double> future_price{};
        std::optional<double> volatility;
        std::optional<double> fra_rate;

        Future(const double eff_date, const double mat_date, const std::optional<double> fwd_rate = std::nullopt,
            const std::optional<double> price = std::nullopt , const std::optional<double> vol = std::nullopt)
            : effective_date(eff_date),
              maturity_date(mat_date),
              future_price(price),
              volatility(vol),
              fra_rate(fwd_rate) {}

        [[nodiscard]] double fra_market_rate() const;
        [[nodiscard]] double npv(const YieldCurve &y_curve)const ;
    };

    struct Swap {
        double maturity;
        double fixedRate;
        double fixedLeg_freq;
        double floatLeg_freq;

        struct Schedule {
            std::vector<double> fixed;
            std::vector<double> floating;
        };

        [[nodiscard]] Schedule buildSchedule() const;

        [[nodiscard]] double npv(const YieldCurve &y_curve , const Schedule &s) const;

    };


    //////////// CDS related data structure ///////////////////////////

    struct CDSMarketData {
        std::string     name;
        Core::Date      effectiveDate;
        Core::Date      valuationDate;
        double          recoveryRate;
        Core::Frequency frequency;
        std::vector<Core::Point> quotes;
    };

    struct CDS{

        std::string Name;
        Core::Date EffectiveDate ;
        Core::Date MaturityDate;
        Core::Date ValuationDate ;
        double Nominal{};
        Core::Frequency frequency;
        Core::DayCount conventionalDayCount;
        std::optional<double> ContractualSpread{};
        std::optional<double> FixedSpread{};
        std::optional<double> Upfront{};
        double RecoveryRate{} ;

        [[nodiscard]] int get_frequency() const;

        [[nodiscard]] double get_maturity() const;


        struct CDSGrids {
            std::vector<Core::Date> defaultTimes;
            std::vector<Core::Date> premiumTimes;
        };

        [[nodiscard]] CDSGrids buildCDSGrids(int default_freq) const;

        static Core::Date firstIMMOnOrAfter(const Core::Date& ref_date) {
            auto [y, m, d] = ref_date.toGregorian();

            int imm_month;
            int imm_year = y;

            if      (m < 3  || (m == 3  && d <= 20)) { imm_month = 3;  }
            else if (m < 6  || (m == 6  && d <= 20)) { imm_month = 6;  }
            else if (m < 9  || (m == 9  && d <= 20)) { imm_month = 9;  }
            else if (m < 12 || (m == 12 && d <= 20)) { imm_month = 12; }
            else { imm_month = 3; imm_year++; }

            return {imm_year, imm_month, 20};
        }

        static Core::Date computeIMMMaturityDate(const Core::Date& effective_date, const double time_in_years) {

            const int n_quarters = static_cast<int>(std::round(time_in_years * 4.0));

            const Core::Date anchor = firstIMMOnOrAfter(effective_date);

            auto [a_year, a_month, a_day] = anchor.toGregorian();

            int mat_month = a_month + n_quarters * 3;
            int mat_year  = a_year  + mat_month / 12;
            mat_month     = mat_month % 12;

            if (mat_month == 0) {
                mat_month = 12;
                mat_year--;
            }

            return {mat_year, mat_month, 20};
        }


    };






}
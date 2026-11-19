//
// Created by ricar on 10/04/2026.
//

#pragma once

#include <string>
#include <vector>

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
        double effective_date;
        double maturity_date;
        double price;
        double volatility;
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


    struct CDS_k {
        std::string underlier;
        double      nominal;
        double      maturity;
        double      contractualSpread;
        double      recoveryRate;
        double      frequency;
    };

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
        Core::Date ValuationDate ;
        double maturity{};
        double Nominal{};
        Core::Frequency frequency;
        Core::DayCount conventionalDayCount;
        double ContractualSpread{};
        double RecoveryRate{} ;

        [[nodiscard]] int get_frequency() const;
        [[nodiscard]] Core::Date get_MaturityDate() const;

        struct CDSGrids {
            std::vector<Core::Date> defaultTimes;
            std::vector<Core::Date> premiumTimes;
        };

        [[nodiscard]] CDSGrids buildCDSGrids(int default_freq) const;

    };



}
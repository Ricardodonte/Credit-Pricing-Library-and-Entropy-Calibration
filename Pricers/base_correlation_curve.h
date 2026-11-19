//
// Created by ricar on 17/04/2026.
//

#pragma once
#include <functional>

#include "Core/numericals.h"
#include "Market/BaseCorrelationCurve.h"
#include "Pricers/base_tranche_pricer.h"

namespace Pricer {

    template <typename Pricer>
    class BaseCorrelationBoot {

    public :

        static_assert( std::is_base_of_v<BaseTranchePricer,Pricer>, "Pricer should herit from Base tranche pricer");
        explicit BaseCorrelationBoot(Pricer &pricer , const Market::YieldCurve &y_curve , const double brent_tol = 1e-8 , const int brent_max = 200,
            const double rho_low = 1e-6 , const double rho_high =  1- 1e-6) :
            m_pricer(pricer), m_y_curve(y_curve), m_solver(Core::Brent(brent_tol , brent_max)) , m_rho_low(rho_low), m_rho_high(rho_high) {

        }

        Market::BaseCorrBootstrapResult bootstrap (const Market::Tranches_MarketData& mdata) {

            const auto & tranches = mdata.quoted_tranches;
            if (tranches.empty())
                throw std::runtime_error("No quoted tranches in the given market data");

            for (std::size_t i = 1 ; i < tranches.size() ; ++i) {
                if (tranches[i].K2 <= tranches[i-1].K2)
                    throw std::invalid_argument("boostrap : Quoted tranches should detachement points should be ordered");
            }

            Market::BaseCorrBootstrapResult boot_res ;

            for(std::size_t m = 0 ; m < tranches.size() ; ++m) {

                const Market::Index_tranche &tr = tranches[m] ;
                Market::TranchesGrid grid = Market::build_time_grid(tr, m_y_curve);

                const double rho_below = (m==0) ? 0.0 : boot_res.curve.rho(tr.K1);

                auto objective = [&](const double rho2) -> double {
                  return tranche_npv(tr, grid,rho_below , rho2);
                };

                const double f_low = objective(m_rho_low);
                const double f_hg = objective(m_rho_high);

                double rho_calib;

                if (f_low * f_hg >0.0) {
                    rho_calib = find_root_bracket(objective , m_rho_low , m_rho_high);
                }
                else {
                    rho_calib = m_solver.solve(objective , m_rho_low , m_rho_high);
                }

                boot_res.curve.add_point(tr.K2 , rho_calib);
                const double pv_check = tranche_npv(tr,grid,rho_below , rho_calib);
                boot_res.residuals.push_back(std::abs(pv_check));

                const double par = m_pricer.par_spread(tr , grid, rho_below , rho_calib);
                boot_res.par_spreads.push_back(par);
            }

            return boot_res;

        }

    private :

        Pricer & m_pricer ;
        const Market::YieldCurve & m_y_curve ;
        Core::Brent m_solver ;
        const double m_rho_low ;
        const double m_rho_high ;

        [[nodiscard]] static Market::Index_tranche make_base_tranche (const Market::Index_tranche &tranche , const double K) {

            Market::Index_tranche copy_tranche = tranche;
            copy_tranche.K1 = 0.0 ;
            copy_tranche.K2 = K ;
            return copy_tranche;

        }

        [[nodiscard]] std::pair<double,double> base_tranche_npv_raw(const Market::Index_tranche &tranche ,
            const Market::TranchesGrid &grid , double rho) const {

            const double K2  = tranche.K2 ;

            const std::size_t N_prem = grid.premium_times.size();
            double pv_premium = 0.0;
            double Q_prev = 1.0;

            for (std::size_t i = 0 ; i < N_prem ; ++i) {
                const double t_i = grid.premium_times[i] ;
                const double DF_i = grid.premium_dFactors[i];
                const double tau = grid.premium_accrual[i];
                const double eml = m_pricer.expected_min_loss(K2,t_i,rho);
                const double Q_i = 1.0 - eml / K2 ;
                pv_premium += tau * DF_i * (Q_prev + Q_i ) ;
                Q_prev = Q_i ;
            }

            const std::size_t N_prot = grid.default_times.size();
            double pv_protection = 0.0;
            Q_prev = 1.0;
            double DF_prev = 1.0;

            for (std::size_t i = 0 ; i < N_prot ; ++i) {
                const double t_i  = grid.default_times[i] ;
                const double DF_i = grid.default_dFactors[i] ;
                const double eml = m_pricer.expected_min_loss(K2,t_i,rho);
                const double Q_i = 1.0 - eml / K2 ;
                const double DF_mid = 0.5 * (DF_prev + DF_i) ;
                pv_protection  += DF_mid * (Q_prev - Q_i) ;
                Q_prev = Q_i ;
                DF_prev = DF_i ;
            }

            return {pv_premium , pv_protection} ;
        }

        [[nodiscard]] double tranche_npv(const Market::Index_tranche &tranche , const Market::TranchesGrid &grid , double const rho1, const double rho2) const {

            const double S = tranche.fair_spread ;

            if (tranche.K1 < 1e-12) {
                auto [prem , prot ] = base_tranche_npv_raw(tranche , grid , rho2 );
                if(tranche.quoted_upfront) {
                    double const npv = tranche.nominal * ( 0.5 * prem * tranche.contractual_spread + tranche.upfront - prot );
                    return npv;
                }
                return tranche.nominal * (0.5 * prem * S - prot) ;
            }

            const double kappa = tranche.K2 / (tranche.K2 - tranche.K1) ;

            const auto tr_K2 = make_base_tranche(tranche , tranche.K2) ;
            const auto tr_K1 = make_base_tranche(tranche , tranche.K1) ;

            auto [prem_1 , prot_1] = base_tranche_npv_raw(tr_K1 , grid , rho1);
            auto [prem_2 , prot_2] = base_tranche_npv_raw(tr_K2 , grid , rho2);

            if (tranche.quoted_upfront) {

                const double value_2 = kappa* (0.5* tranche.contractual_spread * prem_2  - prot_2)  ;
                const double value_1 = (1-kappa) * (0.5 * tranche.contractual_spread * prem_1 - prot_1) ;
                return tranche.nominal * (value_2 + value_1 + tranche.upfront ) ;
            }

            const double value_2 = kappa * (0.5 * S * prem_2 - prot_2) ;
            const double value_1 = (1-kappa) * (0.5 * S * prem_1 - prot_1) ;
            return tranche.nominal * (value_2 + value_1  ) ;

        }

        double find_root_bracket(const std::function<double(double)>& obj , const double low , const double hg) {

            constexpr int N_SCAN = 100 ;
            double prev = low;
            double f_prev = obj(low);
            for (int i = 1; i<= N_SCAN; ++i) {
                const double x = low + (hg - low) * i /N_SCAN;
                const double fx = obj(x);
                if (f_prev * fx <= 0.0) {
                    return m_solver.solve(obj,prev,x);
                }
                prev = x;
                f_prev = fx;
            }
            throw std::runtime_error("BaseCorrBootstraper : not finding the root , check market data") ;
        }

    };

}

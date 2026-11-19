//
// Created by ricar on 10/04/2026.
//

#pragma once
#include <vector>
#include "../../Instruments/CDS/instruments.h"
#include "Core/Dates.h"
#include "Core/types.h"
#include "Core/numericals.h"


namespace Market {

class YieldCurve {

public:

    explicit YieldCurve(const Core::Date refDate) {
        m_refDate = refDate;
    }
    void add_pillar(double t, double df);
    void pop_pillar();
    [[nodiscard]] double discount(double t) const;
    [[nodiscard]] double forward_rate(double t1, double t2) const;
    [[nodiscard]] int    num_pillars() const { return static_cast<int>(m_pillars.size()); }
    [[nodiscard]] const std::vector<Core::Point>& pillars() const { return m_pillars; }

private:

    Core::Date m_refDate  ;
    std::vector<Core::Point> m_pillars {};
};

    class YieldCurveBoot {
        public:

        explicit YieldCurveBoot(const Core::Date refDate)
        : m_curve(refDate) , m_brent(1e-8,100) {
        }

        void add_deposit(const Deposit& dep) {
            m_curve.add_pillar(dep.maturity, dep.implied_df());
        }

        void add_future(const Future& fut) {

            const double df = solve_df(fut.maturity_date, [&](const YieldCurve& yc)-> double {
                return fut.npv(yc);
            } );

            m_curve.add_pillar(fut.maturity_date, df);
        }

        void add_swap(const Market::Swap& sw) {
            const auto sched = sw.buildSchedule();
            const double df  = solve_df(sw.maturity, [&](const YieldCurve& yc)->double {
                return sw.npv(yc,sched);
            });
            m_curve.add_pillar(sw.maturity, df);
        }

        void add_deposits(const std::vector<Deposit>&   deps)  { for (auto& d : deps)  add_deposit(d); }
        void add_futures (const std::vector<Future>& futures)  { for (auto& f : futures)  add_future(f);  }
        void add_swaps   (const std::vector<Swap>&        swaps) { for (auto& s : swaps) add_swap(s);    }

        [[nodiscard]] const YieldCurve& curve() const { return m_curve; }


        private :

        YieldCurve m_curve;
        Core::Brent m_brent ;

        template<typename F>
        double solve_df(const double maturity, F&& objective) {

                auto try_df = [&](const double df_guess) -> double {
                    m_curve.add_pillar(maturity, df_guess);
                    const double val = objective(m_curve);
                    m_curve.pop_pillar();
                    return val;
                };

                const double df_sol = m_brent.solve(try_df, 1e-12, 1);
                return df_sol;
            }

    };





}
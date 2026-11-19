#pragma once
#include <vector>
#include <concepts>
#include "../../Market/Curves/CDS/CreditCurve.h"
#include "../../Market/Instruments/CDS/instruments.h"
#include "../../Pricers/Tranches/Base/base_tranche_pricer.h"
#include "../../Pricers/Tranches/Methods/lhp_pricer.h"
#include "../../Pricers/Tranches/Methods/gaussian_pricer.h"
#include "../../Pricers/Tranches/Methods/full_recursion_pricing.h"
#include "../../Pricers/Tranches/Methods/binomial_pricer.h"

namespace Risk {

struct Uniform_Recovery  { double value; };
struct Multiple_Recovery { std::vector<double> values; };

using SingleMarketData   = Market::CDSMarketData;
using MultipleMarketData = std::vector<Market::CDSMarketData>;

template <typename RecoveryType>
struct AdjustedBinomialPricer;

template <>
struct AdjustedBinomialPricer<Uniform_Recovery> : Pricer::BaseTranchePricer {

    AdjustedBinomialPricer(const std::vector<Market::CreditCurve>& curves,
                            const int n, const double rr)
        : impl(curves, n, rr) {}

    [[nodiscard]] double expected_min_loss(const double K, const double t, const double rho) const override {
        return impl.expected_min_loss(K, t, rho);
    }

    Pricer::AdjustedBinomialPricer impl;
};

template <>
struct AdjustedBinomialPricer<Multiple_Recovery> : Pricer::BaseTranchePricer {

    AdjustedBinomialPricer(const std::vector<Market::CreditCurve>& curves,
                            const int n, const std::vector<double>& rr)
        : impl(curves, n, rr) {}

    [[nodiscard]] double expected_min_loss(const double K, const double t, const double rho) const override {
        return impl.expected_min_loss(K, t, rho);
    }

    Pricer::AdjustedBinomialPricer impl;
};

template <typename P>
struct PricerTraits;

template <>
struct PricerTraits<Pricer::lhp_pricer> {
    using CurveInput = Market::CreditCurve;
    using MDInput    = SingleMarketData;
    using RRInput    = Uniform_Recovery;

    static Pricer::lhp_pricer make(const CurveInput& curve, int, const RRInput& rr) {
        return {curve, rr.value};
    }
};

template <>
struct PricerTraits<Pricer::RecursionPricer> {
    using CurveInput = std::vector<Market::CreditCurve>;
    using MDInput    = MultipleMarketData;
    using RRInput    = Uniform_Recovery;

    static Pricer::RecursionPricer make(const CurveInput& curves, const int n, const RRInput& rr) {
        return Pricer::RecursionPricer(curves, n, rr.value);
    }
};

template <>
struct PricerTraits<Pricer::GaussianPricer> {
    using CurveInput = std::vector<Market::CreditCurve>;
    using MDInput    = MultipleMarketData;
    using RRInput    = Multiple_Recovery;

    static Pricer::GaussianPricer make(const CurveInput& curves, const int n, const RRInput& rr) {
        auto rr_copy = rr.values;
        return Pricer::GaussianPricer(curves, n, rr_copy);
    }
};

template <typename RecoveryType>
struct PricerTraits<AdjustedBinomialPricer<RecoveryType>> {
    using CurveInput = std::vector<Market::CreditCurve>;
    using MDInput    = MultipleMarketData;
    using RRInput    = RecoveryType;

    static AdjustedBinomialPricer<RecoveryType>
    make(const CurveInput& curves, int n, const RRInput& rr) {
        if constexpr (std::is_same_v<RecoveryType, Uniform_Recovery>)
            return {curves, n, rr.value};
        else
            return {curves, n, rr.values};
    }
};

template <typename P>
concept HasTraits = requires(const typename PricerTraits<P>::CurveInput& c,int n,
    const typename PricerTraits<P>::RRInput& rr)
{
    typename PricerTraits<P>::CurveInput;
    typename PricerTraits<P>::MDInput;
    typename PricerTraits<P>::RRInput;
    { PricerTraits<P>::make(c, n, rr) } -> std::same_as<P>;
};

template <typename P>
    requires HasTraits<P>
    P build_pricer(const typename PricerTraits<P>::CurveInput& curves,int n,
        const typename PricerTraits<P>::RRInput& rr)
    {
        return PricerTraits<P>::make(curves, n, rr);
    }

}
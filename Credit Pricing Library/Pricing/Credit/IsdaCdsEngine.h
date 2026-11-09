
#pragma once
#include <stdexcept>

#include "Instruments/Credit/Cds.h"
#include "Pricing/Credit/CdsValuation.h"
#include "Pricing/Credit/PreparedCds.h"

namespace Pricing::Credit {


    /// @brief Prices CDS on a pair of curves. Holds non-owning pointers: both curves must outlive the engine.
    template <class YieldCurveT, class CreditCurveT>
    class IsdaCdsEngine {
    public:
        IsdaCdsEngine(const YieldCurveT& yield_curve, const CreditCurveT& credit_curve) noexcept
            : yield_curve_(&yield_curve), credit_curve_(&credit_curve) {}

        [[nodiscard]] CdsValuation value(const Instruments::Credit::Cds& cds) const {
            if (yield_curve_->reference_date() != cds.trade_date || credit_curve_->reference_date() != cds.trade_date)
                throw std::invalid_argument("IsdaCdsEngine: both curves must be built as of the CDS trade date");

            return PreparedCds(cds, *yield_curve_, credit_curve_->pillar_dates()).value(*credit_curve_);
        }

    private:
        const YieldCurveT* yield_curve_;
        const CreditCurveT* credit_curve_;
    };

} // namespace Pricing::Credit

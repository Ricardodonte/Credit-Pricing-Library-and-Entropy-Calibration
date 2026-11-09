// Ready-to-use bootstrappers. Each one is only an instantiation of the generic SequentialBootstrapper
// with the relevant helpers; a new curve (e.g. base correlation) gets its own alias here.

#pragma once
#include "Core/day_counter.h"
#include "Curves/Bootstrap/CdsHelper.h"
#include "Curves/Bootstrap/RateHelpers.h"
#include "Curves/Bootstrap/SequentialBootstrapper.h"
#include "Curves/CreditCurve.h"
#include "Curves/YieldCurve.h"

namespace Curves::Bootstrap {

    /// @brief Yield curve from deposits, FRAs / futures and swaps (single-curve).
    template <Core::DayCountConvention DC = Core::Actual365Fixed>
    using YieldCurveBootstrapper = SequentialBootstrapper<YieldCurve<DC>, DepositHelper, FraHelper, SwapHelper>;

    /// @brief Credit curve from standard CDS quotes, discounted on a YieldCurveT.
    template <class YieldCurveT, Core::DayCountConvention DC = Core::Actual365Fixed>
    using CreditCurveBootstrapper = SequentialBootstrapper<CreditCurve<DC>, CdsHelper<YieldCurveT>>;

} // namespace Curves::Bootstrap

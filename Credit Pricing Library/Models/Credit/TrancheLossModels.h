// Ready-to-use tranche loss models. Each one is a combination of the building blocks of Models/Credit; a new
// approximation only needs a ConditionalLossModel and an alias here.
//   Model                         Accuracy                         Cost per E[min(L, K)]
//   LhpModel                      large homogeneous pool only      closed form (one bivariate normal)
//   GaussianCopulaNormal          large granular pools             20 x names
//   GaussianCopulaAdjBinomial     good, exact if homogeneous       20 x (names + defaults below K)
//   GaussianCopulaRecursion       exact on the loss lattice        20 x names x K / loss unit

#pragma once
#include "Models/Credit/Conditional/AdjustedBinomial.h"
#include "Models/Credit/Conditional/LossRecursion.h"
#include "Models/Credit/Conditional/NormalApproximation.h"
#include "Models/Credit/GaussianCopula.h"
#include "Models/Credit/LargeHomogeneousPool.h"
#include "Models/Credit/LossModel.h"

namespace Models::Credit {

    using LhpModel = LargeHomogeneousPool;
    using GaussianCopulaNormal = GaussianCopula<NormalApproximation>;
    using GaussianCopulaAdjBinomial = GaussianCopula<AdjustedBinomial>;
    using GaussianCopulaRecursion = GaussianCopula<LossRecursion>;

    static_assert(TrancheLossModel<LhpModel>);
    static_assert(TrancheLossModel<GaussianCopulaNormal>);
    static_assert(TrancheLossModel<GaussianCopulaAdjBinomial>);
    static_assert(TrancheLossModel<GaussianCopulaRecursion>);

} // namespace Models::Credit

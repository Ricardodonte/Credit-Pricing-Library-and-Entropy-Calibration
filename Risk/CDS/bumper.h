//
// Created by ricar on 11/05/2026.
//

#pragma once
#include <vector>

namespace Risk {

    struct CDSBumpConfig {
        bool compute_CreditDV01        = true;
        bool compute_YieldDV01         = true;
        bool compute_SpreadGamma       = true;
        bool compute_Theta             = false;
        bool compute_Bucket_CreditDV01 = false;
        bool use_central_diff          = true;
        double h_spread                = 1e-4;
        double h_rate                  = 1e-4;
        double h_theta                 = 1.0;
    };

}

//
// Created by ricar on 08/05/2026.
//

#pragma once
#include <vector>
#include <string>

namespace Risk {

namespace BumpSize {
    inline constexpr double CS01_BP    = 1e-4;
    inline constexpr double DV01_BP    = 1e-4;
    inline constexpr double RHO_BUMP   = 0.01;
    inline constexpr double THETA_DAYS = 1.0;
}


struct ParallelBump {
    double h = BumpSize::CS01_BP;
};

struct BucketedBump {
    std::size_t bucket_idx = 0;
    double      h          = BumpSize::CS01_BP;
};

struct RhoBump {
    double h = BumpSize::RHO_BUMP;
};

struct RateBump {
    double h = BumpSize::DV01_BP;
};

struct ThetaBump {
    double days = BumpSize::THETA_DAYS;
};

struct TranchesBumpConfig {
    bool compute_cs01         = true;
    bool compute_dv01         = true;
    bool compute_rho01        = true;
    bool compute_bucketed_cs01= true;
    bool compute_gamma        = true;
    bool compute_theta        = false;
    bool use_central_diff     = true;

    std::vector<double> cs01_pillars = {1.0, 3.0, 5.0, 7.0, 10.0};

    double h_spread = BumpSize::CS01_BP;
    double h_rate   = BumpSize::DV01_BP;
    double h_rho    = BumpSize::RHO_BUMP;
};

}

cmake_minimum_required(VERSION 4.2)
project(CDS)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(CDS main.cpp
        Core/Dates.cpp
        Core/Dates.h
        Core/numericals.h
        Core/types.h
        Market/instruments.cpp
        Market/instruments.h
        Market/YieldCurve.cpp
        Market/YieldCurve.h
        Market/CreditCurve.cpp
        Market/CreditCurve.h
        Pricers/cds_pricers.cpp
        Pricers/cds_pricers.h
        Market/tranches_instruments.h
        Market/BaseCorrelationCurve.cpp
        Market/BaseCorrelationCurve.h
        Pricers/base_tranche_pricer.cpp
        Pricers/base_tranche_pricer.h
        Pricers/lhp_pricer.cpp
        Pricers/lhp_pricer.h
        Pricers/full_recursion_pricing.cpp
        Pricers/full_recursion_pricing.h
        Pricers/gaussian_pricer.cpp
        Pricers/gaussian_pricer.h
        Pricers/base_correlation_curve.h
        Pricers/tranche_book_pricer.cpp
        Pricers/tranche_book_pricer.h
        Pricers/binomial_pricer.cpp
        Pricers/binomial_pricer.h
)

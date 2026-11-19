cmake_minimum_required(VERSION 3.16)
project(CDS CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    Core/Dates.cpp Core/Dates.h Core/numericals.h Core/types.h
    Market/Instruments/CDS/instruments.cpp Market/Instruments/CDS/instruments.h
    Market/Instruments/Tranches/tranches_instruments.h
    Market/Curves/YieldCurve/YieldCurve.cpp Market/Curves/YieldCurve/YieldCurve.h
    Market/Curves/CDS/CreditCurve.cpp Market/Curves/CDS/CreditCurve.h
    Market/Curves/Tranches/BaseCorrelationCurve.cpp Market/Curves/Tranches/BaseCorrelationCurve.h
    Market/Calibration/base_correlation_curve.h
    Pricers/CDS/cds_pricers.cpp Pricers/CDS/cds_pricers.h
    Pricers/Tranches/Base/base_tranche_pricer.cpp Pricers/Tranches/Base/base_tranche_pricer.h
    Pricers/Tranches/Methods/lhp_pricer.cpp Pricers/Tranches/Methods/lhp_pricer.h
    Pricers/Tranches/Methods/full_recursion_pricing.cpp Pricers/Tranches/Methods/full_recursion_pricing.h
    Pricers/Tranches/Methods/gaussian_pricer.cpp Pricers/Tranches/Methods/gaussian_pricer.h
    Pricers/Tranches/Methods/binomial_pricer.cpp Pricers/Tranches/Methods/binomial_pricer.h
    Pricers/tranche_book_pricer.cpp Pricers/tranche_book_pricer.h
    Risk/Tranches/risk_engine.h Risk/Tranches/pricers_traits.h Risk/Tranches/risk_result.h Risk/Tranches/bumper.h
    Risk/CDS/risk_engine.cpp Risk/CDS/risk_engine.h Risk/CDS/bumper.h
    interface.cpp interface.h
        KL_pricer.cpp
        KL_pricer.h
)

add_executable(CDS main.cpp ${SOURCES})
target_include_directories(CDS PRIVATE ${PROJECT_SOURCE_DIR})



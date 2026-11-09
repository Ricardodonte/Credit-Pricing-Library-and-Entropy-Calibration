
cmake_minimum_required(VERSION 3.23)
project(Credit_Pricing_Library LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Credit_Pricing_Library INTERFACE)
target_compile_features(Credit_Pricing_Library INTERFACE cxx_std_20)
target_sources(Credit_Pricing_Library INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
            # Core : dates, conventions, outils numériques
            Core/Dates.h
            Core/Period.h
            Core/Schedule.h
            Core/ImmDates.h
            Core/calendar.h
            Core/day_counter.h
            Core/interpolator.h
            Core/numericals.h
            # Curves : courbes et leur bootstrap
            Curves/BaseCurves.h
            Curves/YieldCurve.h
            Curves/CreditCurve.h
            Curves/BaseCorrelationCurve.h
            Curves/Bootstrap/BootstrapTypes.h
            Curves/Bootstrap/BootstrapTraits.h
            Curves/Bootstrap/SequentialBootstrapper.h
            Curves/Bootstrap/RateHelpers.h
            Curves/Bootstrap/CdsHelper.h
            Curves/Bootstrap/CurveBootstrappers.h
            Curves/Bootstrap/TrancheHelper.h
            Curves/Bootstrap/BaseCorrelationBootstrapper.h
            # Instruments : données contractuelles, par classe d'actifs (Credit/ accueillera Tranche, CLO, ...)
            Instruments/Rates/Deposit.h
            Instruments/Rates/Fra.h
            Instruments/Rates/Swap.h
            Instruments/Credit/Cds.h
            Instruments/Credit/CdsQuote.h
            Instruments/Credit/CreditIndex.h
            Instruments/Credit/Tranche.h
            Instruments/Credit/TrancheQuote.h
            # Models : modèles de perte de portefeuille (copule, LHP, approximations conditionnelles)
            Models/Credit/LossModel.h
            Models/Credit/GaussianCopula.h
            Models/Credit/FactorQuadrature.h
            Models/Credit/LargeHomogeneousPool.h
            Models/Credit/TrancheLossModels.h
            Models/Credit/Conditional/NormalApproximation.h
            Models/Credit/Conditional/AdjustedBinomial.h
            Models/Credit/Conditional/LossRecursion.h
            # Pricing : moteurs de valorisation, par classe d'actifs
            Pricing/Rates/DiscountingPricer.h
            Pricing/Credit/CdsIntegrationGrid.h
            Pricing/Credit/UpfrontValuation.h
            Pricing/Credit/CdsValuation.h
            Pricing/Credit/PreparedCds.h
            Pricing/Credit/IsdaCdsEngine.h
            Pricing/Credit/ReferencePool.h
            Pricing/Credit/TrancheIntegrationGrid.h
            Pricing/Credit/PreparedTranche.h
)

function(credit_enable_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endfunction()

add_executable(Credit_Pricing_App main.cpp)
target_link_libraries(Credit_Pricing_App PRIVATE Credit_Pricing_Library)
credit_enable_warnings(Credit_Pricing_App)

enable_testing()
add_executable(Credit_Pricing_Tests
        tests/test_main.cpp
        tests/test_bootstrap.cpp
        tests/test_tranches.cpp
)
target_link_libraries(Credit_Pricing_Tests PRIVATE Credit_Pricing_Library)
credit_enable_warnings(Credit_Pricing_Tests)
add_test(NAME Credit_Pricing_Tests COMMAND Credit_Pricing_Tests)

add_executable(Credit_Pricing_Benchmark bench/benchmark.cpp)
target_link_libraries(Credit_Pricing_Benchmark PRIVATE Credit_Pricing_Library)
credit_enable_warnings(Credit_Pricing_Benchmark)

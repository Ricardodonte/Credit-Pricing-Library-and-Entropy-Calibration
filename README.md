# Credit Derivatives Pricing and Entropy Calibration

This repository brings together two parts of my master's thesis on the pricing of credit default swaps and index tranches. The first is a C++20 library for curve construction and credit derivatives pricing. The second is a Python implementation of a non-parametric approach to calibrating portfolio loss distributions by minimizing relative entropy.

## Credit derivatives pricing library

The [C++ library](./Credit%20Derivatives%20Pricing%20Library) is header-only. It separates instruments, curves, loss models and pricing engines so that the same pricing components can be used with different portfolio loss models.

- **Curve construction.** Bootstrap discount curves from rate instruments, survival curves from CDS quotes and a base correlation curve from tranche upfronts.
- **CDS pricing.** Compute premium and protection legs, par spreads and upfronts with an ISDA-style pricing engine. For repeated valuations of the same contract, `PreparedCds` allows the pricing grid to be reused.
- **Index tranche pricing.** Value tranches under a one-factor Gaussian copula using the Large Homogeneous Pool approximation, the conditional normal approximation, the adjusted binomial approximation or loss recursion.
- **Examples and validation.** The repository includes a pricing example, tests for curve construction and credit pricing, and a benchmark executable.

To build and run the tests from the repository root, use a C++20 compiler and CMake 3.23 or later.

```sh
cmake -S "Credit Derivatives Pricing Library" -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Kullback–Leibler calibration and pricing

The [Python application](./Kullback%20based%20calibration%20and%20pricing) calibrates portfolio loss marginals at a set of dates from observable tranche upfronts. Among the distributions that fit, or approximately fit, the quotes, the method selects those closest to a prior in weighted Kullback–Leibler divergence.

The implementation compares four formulations.

| Formulation | Calibration conditions |
| --- | --- |
| **A** | Matches the tranche pricing constraints exactly. |
| **Regularized A** | Penalizes deviations from the quoted upfronts. |
| **A++** | Adds a stochastic ordering constraint across calibration dates to the regularized formulation. At each loss threshold, the probability of exceeding that threshold must be non-decreasing over time. |
| **Anchored A++** | Adds a penalty for deviations from an expected portfolio loss term structure implied by the survival curve. |

The code solves the dual calibration problems and computes local price sensitivities by differentiating the optimality conditions. For the calendar-constrained variants, the derivative is computed where the active constraints are locally stable.

### Synthetic CDX 5Y experiment

The experiment uses a synthetic five-year CDX North America Investment Grade portfolio with 125 equally weighted names and a 60% recovery rate. A common-shock model generates reference upfronts. Five tranches are used for calibration, and the **4–9%** and **17–22%** tranches are priced out of sample.

Each formulation is run with two priors. The first is a finite-portfolio Gaussian prior built from a single LHP correlation fitted to the five synthetic quotes. The second is uniform over the 126 possible default counts. Base correlation is calibrated to the same five upfronts and uses the same survival curve.

The notebook compares the upfronts against the synthetic reference, measures calendar violations and checks local quote sensitivities against a **+1 bp bump followed by recalibration and repricing**.

### Run the notebook

```sh
cd "Kullback based calibration and pricing"
python -m pip install numpy notebook
python -m notebook cdx-5y-calibration-diagnostic.ipynb
```

Open [the notebook](./Kullback%20based%20calibration%20and%20pricing/cdx-5y-calibration-diagnostic.ipynb) and run the cells in order. The three Python modules should stay in the same directory.

| Module | Contents |
| --- | --- |
| `application.py` | Sets up the synthetic CDX case, constructs the calibration problems and computes quote sensitivities. |
| `market.py` | Defines the market conventions, discount curve, tranche cash flows and synthetic reference model. |
| `models.py` | Implements the LHP and base correlation benchmarks, the priors, the entropy solvers and the sensitivity equations. |

## README disclosure

This README was written by ChatGPT after reviewing the code, notebooks, and project structure in this repository.

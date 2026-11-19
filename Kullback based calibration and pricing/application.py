"""Synthetic quarterly CDX application in the notation of the thesis."""

from dataclasses import dataclass

import numpy as np

from market import Cashflows, CommonShock, OisCurve, Portfolio, Tranche
from models import (
    BaseCorrelation, EntropicCalibration, Fit, GaussianCountPrior, LhpPricer,
    UniformCountPrior, feasible_marginals,
)


BENCHMARKS = (
    Tranche.from_percent(0, 3, coupon_bp=500),
    Tranche.from_percent(3, 7),
    Tranche.from_percent(7, 10),
    Tranche.from_percent(10, 15),
    Tranche.from_percent(15, 30),
)
TARGETS = (Tranche.from_percent(4, 9), Tranche.from_percent(17, 22))
QUOTE_PRECISION = 100_000.
LOSS_PRECISION = 1_000_000.


@dataclass(frozen=True)
class CalibrationRun:
    """One fitted calibration and its prior label."""

    name: str
    prior: str
    problem: EntropicCalibration
    fit: Fit

    @property
    def marginals(self):
        """Use the raw tilt or its numerically calendar-feasible version for pricing."""
        return feasible_marginals(self.fit.q) if self.fit.calendar_multiplier.size else self.fit.q


class CdxApplication:
    """Construct and calibrate the single synthetic CDX 5Y example."""

    def __init__(self):
        self.portfolio = Portfolio(names=125, recovery=.60)
        self.cashflows = Cashflows(OisCurve())
        self.grid = self.cashflows.grid()
        self.reference_model = CommonShock(self.portfolio)
        self.reference_marginals = self.reference_model.marginals(self.grid.times)
        self.survival = self.reference_model.survival(self.grid.times)
        self.quote_kernels = self.kernels(BENCHMARKS)
        self.quotes = np.array([self.reference(t).upfront for t in BENCHMARKS])
        self.A = np.array([kernel.row for kernel in self.quote_kernels])
        self.b = np.array([kernel.rhs(u) for kernel, u in zip(self.quote_kernels, self.quotes)])
        self.weights = np.diff(self.grid.times) / self.grid.times[-1]
        self.mean_loss = (1 - self.portfolio.recovery) * (1 - self.survival[1:])

        self.lhp = LhpPricer(self.grid, self.portfolio, self.survival)
        self.lhp_fit, andersen = self.andersen_prior()
        self.priors = {
            "Andersen": andersen,
            "Uniforme": UniformCountPrior(self.portfolio)(len(self.weights)),
        }

    def andersen_prior(self):
        """Fit the LHP correlation and construct the finite-portfolio prior."""
        fit = self.lhp.fit_correlation(BENCHMARKS, self.quotes)
        prior = GaussianCountPrior(self.portfolio)(self.survival[1:], fit.correlation)
        return fit, prior

    def base_correlation(self):
        """Fit the benchmark on the same five upfronts and survival curve."""
        return BaseCorrelation(self.lhp, BENCHMARKS, self.quotes)

    def overlapping_quote_indices(self, tranche):
        """Return the indices of quoted tranches sharing a positive-width loss interval."""
        return tuple(
            i for i, quoted in enumerate(BENCHMARKS)
            if max(tranche.attachment, quoted.attachment) < min(tranche.detachment, quoted.detachment)
        )

    def kernels(self, tranches):
        """Build the quarterly pricing kernels in the supplied tranche order."""
        return tuple(self.grid.kernel(t, self.portfolio) for t in tranches)

    def reference(self, tranche):
        """Value the contractual legs directly from the synthetic loss law."""
        loss, reduction = tranche.payoffs(self.portfolio.loss, self.portfolio.recovered)
        return self.cashflows.reference_legs(self.reference_marginals @ loss,
                                             self.reference_marginals @ reduction,
                                             tranche.coupon)

    def calibration_problems(self, prior):
        """Construct the four calibration problems for one common prior."""
        n_quotes, n_dates = len(self.quotes), len(self.weights)
        date = np.arange(n_dates)
        C = np.zeros((n_dates, *self.A.shape[1:]))
        C[date, date] = self.portfolio.loss
        augmented_A = np.concatenate((self.A, C))
        augmented_b = np.r_[self.b, self.mean_loss]
        precision = np.diag(np.r_[np.full(n_quotes, QUOTE_PRECISION),
                                  np.full(n_dates, LOSS_PRECISION)])
        specifications = (
            ("A", self.A, self.b, None, False),
            ("A régularisé", self.A, self.b, QUOTE_PRECISION, False),
            ("A++", self.A, self.b, QUOTE_PRECISION, True),
            ("A++ ancré", augmented_A, augmented_b, precision, True),
        )
        return {
            name: EntropicCalibration(matrix, targets, prior, self.weights, penalty, calendar)
            for name, matrix, targets, penalty, calendar in specifications
        }

    def calibrate(self):
        """Fit A, regularized A, A++ and mean-loss-anchored A++ for both priors."""
        for label, prior in self.priors.items():
            for name, problem in self.calibration_problems(prior).items():
                yield CalibrationRun(name, label, problem, problem.solve())

    def _rhs_direction(self, run, upfront, mean_loss):
        discounts = np.array([kernel.settlement_discount for kernel in self.quote_kernels])
        n_mean = run.problem.m - len(discounts)
        dU = np.zeros_like(discounts) if upfront is None else np.asarray(upfront, dtype=float)
        dm = np.zeros(n_mean) if mean_loss is None else np.asarray(mean_loss, dtype=float)
        if dU.shape != discounts.shape or dm.shape != (n_mean,):
            raise ValueError("Provide one direction per quote or anchored date.")
        return np.r_[discounts * dU, dm]

    def sensitivity(self, run, tranches, *, upfront=None, log_prior=None, mean_loss=None):
        """Return analytical clean-upfront derivatives with a locally stable calendar set.

        upfront and mean_loss are expressed in fractions of their respective
        notionals. log_prior is a date-by-state direction for log probabilities.
        Inputs left at None are held fixed.
        """
        db = self._rhs_direction(run, upfront, mean_loss)
        dq = run.problem.direction(run.fit, upfront=db, log_prior=log_prior)
        return np.array([
            np.sum(kernel.row * dq) / kernel.settlement_discount
            for kernel in self.kernels(tranches)
        ])

    def bump_and_reprice(self, run, tranches, *, upfront=None, step=1e-4):
        """Return clean upfronts after negative, zero and positive quote shifts."""
        if not np.isfinite(step) or step <= 0:
            raise ValueError("Bump step must be positive.")
        db = self._rhs_direction(run, upfront, None)
        kernels = self.kernels(tranches)
        q = run.marginals
        base = [kernel.upfront(q) for kernel in kernels]
        prices = []
        for signed_step in (-step, step):
            problem = run.problem.refit(rhs=run.problem.b + signed_step * db)
            bumped = CalibrationRun(run.name, run.prior, problem, problem.solve())
            q = bumped.marginals
            prices.append([kernel.upfront(q) for kernel in kernels])
        return np.array([prices[0], base, prices[1]])

    def bump_sensitivity(self, run, tranches, *, upfront=None, step=1e-4):
        """Return left and right clean-upfront slopes for a quote direction."""
        prices = self.bump_and_reprice(run, tranches, upfront=upfront, step=step)
        return np.diff(prices, axis=0) / step

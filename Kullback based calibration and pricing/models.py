"""LHP prior and relative-entropy marginal calibration."""
from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from math import erfc, sqrt
from statistics import NormalDist
import numpy as np

from market import (
    Legs, Portfolio, Tranche, ValuationGrid, _bracketed_root, binomial_probabilities,
)


_NORMAL = NormalDist()


def normal_cdf(values):
    values = np.asarray(values)
    return np.fromiter(
        (0.5 * erfc(-float(x) / sqrt(2)) for x in values.flat),
        dtype=float, count=values.size,
    ).reshape(values.shape)


@lru_cache
def _legendre(nodes):
    return np.polynomial.legendre.leggauss(nodes)


@lru_cache
def _hermite(nodes):
    return np.polynomial.hermite.hermgauss(nodes)


class LhpPricer:
    """One-factor Gaussian large homogeneous portfolio with deterministic recovery."""

    def __init__(self, grid: ValuationGrid, portfolio: Portfolio, survival, nodes=48):
        self.grid, self.portfolio, self.nodes = grid, portfolio, nodes
        self.pd = 1 - np.asarray(survival, dtype=float)
        if self.pd.shape != grid.times.shape or not np.all((0 <= self.pd) & (self.pd < 1)):
            raise ValueError("Supply survival probabilities on the model grid.")
        self.thresholds = np.array([_NORMAL.inv_cdf(x) if x else -np.inf for x in self.pd[1:]])

    def expected_cap(self, amount: float, cap: float, rho: float) -> np.ndarray:
        """E[min(amount * conditional default fraction, cap)] at each date."""
        if not 0 <= rho < 1:
            raise ValueError("Require asset correlation in [0, 1).")
        if cap <= 0:
            return np.zeros_like(self.pd)
        if cap >= amount:
            return amount * self.pd
        if rho == 0:
            return np.minimum(amount * self.pd, cap)

        x, w = _legendre(self.nodes)
        split = (
            self.thresholds - sqrt(1 - rho) * _NORMAL.inv_cdf(cap / amount)
        ) / sqrt(rho)
        left = np.clip(split, -10, 10)
        half_width = (10 - left) / 2
        z = left[:, None] + half_width[:, None] * (x + 1)
        conditional = normal_cdf(
            (self.thresholds[:, None] - sqrt(rho) * z) / sqrt(1 - rho)
        )
        density = np.exp(-.5 * z**2) / sqrt(2 * np.pi)
        integral = half_width * np.sum(w * density * conditional, axis=1)
        return np.r_[0., cap * normal_cdf(split) + amount * integral]

    def equity(self, detachment, rho):
        lgd, recovery = 1 - self.portfolio.recovery, self.portfolio.recovery
        loss = self.expected_cap(lgd, detachment, rho)
        recovered = recovery * self.pd - self.expected_cap(recovery, 1 - detachment, rho)
        return loss, loss + recovered

    def legs(self, tranche: Tranche, rho: float) -> Legs:
        return self.base_legs(tranche, rho, rho)

    def base_legs(self, tranche: Tranche, rho_attachment: float, rho_detachment: float) -> Legs:
        """Subtract the two equity legs and normalize by tranche width."""
        low_loss, low_reduction = self.equity(tranche.attachment, rho_attachment)
        up_loss, up_reduction = self.equity(tranche.detachment, rho_detachment)
        width = tranche.detachment - tranche.attachment
        return self.grid.legs((up_loss - low_loss) / width,
                              (up_reduction - low_reduction) / width, tranche.coupon)

    def fit_correlation(self, tranches, upfronts) -> PriorFit:
        """Minimize the unweighted sum of squared differences in clean upfronts."""
        upfronts = np.asarray(upfronts, dtype=float)
        if upfronts.shape != (len(tranches),) or len(tranches) == 0:
            raise ValueError("Provide one clean upfront per benchmark tranche.")

        def errors(rho):
            return np.array([self.legs(tranche, rho).upfront for tranche in tranches]) - upfronts

        def loss(rho):
            delta = errors(rho)
            return float(delta @ delta)

        points = np.r_[np.linspace(0, .98, 99), .99, .995, .999]
        scores = np.array([loss(rho) for rho in points])
        candidates = [(points[0], scores[0]), (points[-1], scores[-1])]
        for i in range(1, len(points) - 1):
            if scores[i] <= min(scores[i - 1], scores[i + 1]):
                rho = _golden_minimum(loss, points[i - 1], points[i + 1])
                candidates.append((rho, loss(rho)))
        rho, score = min(candidates, key=lambda pair: pair[1])
        return PriorFit(float(rho), score, errors(rho))


class BaseCorrelation:
    """Bootstrap LHP equity correlations from contiguous tranche upfronts."""

    def __init__(self, lhp: LhpPricer, tranches, upfronts):
        tranches = tuple(tranches)
        upfronts = np.asarray(upfronts, dtype=float)
        if not tranches or upfronts.shape != (len(tranches),) or not np.isfinite(upfronts).all():
            raise ValueError("Provide one finite clean upfront per calibration tranche.")
        self.lhp = lhp
        strikes, correlations = [], []
        scan = np.r_[np.linspace(0, .98, 99), .99, .995, .999]
        for tranche, upfront in zip(tranches, upfronts):
            previous_strike = strikes[-1] if strikes else 0.
            previous_rho = correlations[-1] if correlations else 0.
            if not np.isclose(tranche.attachment, previous_strike, atol=1e-12, rtol=0):
                raise ValueError("Use contiguous calibration tranches starting at zero.")

            def residual(rho):
                return lhp.base_legs(tranche, previous_rho, rho).upfront - upfront

            values = np.array([residual(rho) for rho in scan])
            exact = np.flatnonzero(np.abs(values) <= 1e-12)
            brackets = np.flatnonzero((values[:-1] * values[1:] < 0)
                                      & (np.abs(values[:-1]) > 1e-12)
                                      & (np.abs(values[1:]) > 1e-12))
            if exact.size + brackets.size != 1:
                raise ValueError(f"Calibration requires a unique bracketed root for {tranche.name}.")
            if exact.size:
                rho = scan[exact[0]]
            else:
                i = brackets[0]
                rho = _bracketed_root(residual, scan[i], scan[i + 1])
            strikes.append(tranche.detachment)
            correlations.append(rho)
        self.detachments = np.array(strikes)
        self.correlations = np.array(correlations)

    def rho(self, strike):
        """Interpolate base correlation linearly across strikes."""
        return float(np.interp(strike, self.detachments, self.correlations))

    def legs(self, tranche: Tranche) -> Legs:
        """Use the calibrated base correlation at each tranche endpoint."""
        return self.lhp.base_legs(tranche, self.rho(tranche.attachment), self.rho(tranche.detachment))


def _golden_minimum(function, left, right, tolerance=1e-10):
    ratio = (sqrt(5) - 1) / 2
    a, b = right - ratio * (right - left), left + ratio * (right - left)
    fa, fb = function(a), function(b)
    while right - left > tolerance:
        if fa > fb:
            left, a, fa = a, b, fb
            b = left + ratio * (right - left)
            fb = function(b)
        else:
            right, b, fb = b, a, fa
            a = right - ratio * (right - left)
            fa = function(a)
    return (left + right) / 2


@dataclass(frozen=True)
class PriorFit:
    correlation: float
    objective: float
    upfront_errors: np.ndarray


class GaussianCountPrior:
    """Integrate the finite-portfolio binomial (homogeneous Andersen recursion)."""

    def __init__(self, portfolio: Portfolio, nodes=256):
        self.portfolio, self.nodes = portfolio, nodes

    def __call__(self, survival, rho):
        p = 1 - np.asarray(survival, dtype=float)
        if rho == 0:
            return binomial_probabilities(self.portfolio.names, p)
        x, w = _hermite(self.nodes)
        thresholds = np.array([_NORMAL.inv_cdf(value) for value in p])
        probabilities = normal_cdf(
            (thresholds[:, None] - sqrt(2 * rho) * x) / sqrt(1 - rho)
        )
        pmf = binomial_probabilities(self.portfolio.names, probabilities)
        q = np.einsum("j,ijk->ik", w / sqrt(np.pi), pmf)
        if np.any(q <= 0):
            raise FloatingPointError("Gaussian prior has an unsupported state at this precision.")
        return q / q.sum(axis=1, keepdims=True)


class UniformCountPrior:
    def __init__(self, portfolio: Portfolio):
        self.portfolio = portfolio

    def __call__(self, dates):
        n = self.portfolio.names + 1
        return np.full((dates, n), 1 / n)


def tails(q):
    """Return P(N >= k) for k=1,...,n, with dates on the first axis."""
    return np.cumsum(q[:, :0:-1], axis=1)[:, ::-1]


def calendar_gaps(q):
    """Return each tail probability at the next date minus its value at the previous date."""
    return np.diff(tails(q), axis=0)


def calendar_adjoint(multiplier, shape):
    """Transpose of the temporal difference of probability tails."""
    increments = np.zeros(shape)
    increments[:-1, 1:] -= multiplier
    increments[1:, 1:] += multiplier
    return np.cumsum(increments, axis=1)


def feasible_marginals(q):
    """Rearrange each threshold's tails in increasing date order."""
    ordered = np.sort(tails(q), axis=0)
    return -np.diff(np.column_stack((np.ones(len(q)), ordered, np.zeros(len(q)))), axis=1)


def _absolute_max(array):
    return float(np.max(np.abs(array), initial=0.))


@dataclass(frozen=True)
class Fit:
    """Calibrated dual state and numerical diagnostics.

    q is the raw exponential tilt. market_multiplier follows the order of
    all target rows, including expected-loss rows if present. Calendar
    multipliers have shape (date minus one, thresholds) when enabled.
    For A++, the gap uses the feasible marginals obtained by sorting tails;
    feasible_calendar_gap is the maximum change in a probability from that step.
    """
    q: np.ndarray
    market_multiplier: np.ndarray
    calendar_multiplier: np.ndarray
    iterations: int
    quote_stationarity: float
    calendar_violation: float
    complementarity: float
    primal_dual_gap: float | None
    feasible_calendar_gap: float | None = None


class EntropicCalibration:
    """Weighted datewise KL calibration in the notation of the thesis.

    A has axes (target, date, default count), b is a vector of target values,
    and prior holds strictly positive probabilities without the fixed day-zero
    state. weights are positive date weights. A positive scalar precision is
    applied to every target; a matrix gives the full quadratic penalty. With
    precision=None the targets are exact. calendar=True enforces increasing
    tail probabilities between adjacent dates and requires a penalty matrix.
    """

    def __init__(self, A, b, prior, weights, precision=None, calendar=False):
        self.A, self.b = np.asarray(A, dtype=float), np.asarray(b, dtype=float)
        p, w = np.asarray(prior, dtype=float), np.asarray(weights, dtype=float)
        if (
            p.ndim != 2 or p.shape[1] < 2 or self.A.ndim != 3
            or self.A.shape[1:] != p.shape or self.b.shape != (self.A.shape[0],)
            or w.shape != (len(p),) or not all(np.isfinite(x).all() for x in (self.A, self.b, p, w))
        ):
            raise ValueError("Inconsistent shapes or non-finite calibration inputs.")
        if np.any(p <= 0) or not np.allclose(p.sum(axis=1), 1.) or np.any(w <= 0):
            raise ValueError("Prior must be positive and normalized, with positive date weights.")
        if precision is None and calendar:
            raise ValueError("A++ uses a positive definite quadratic penalty.")
        self.p, self.w = p, w
        self.log_prior = np.log(p)
        self.m = len(self.b)
        self.calendar_shape = (len(p) - 1, p.shape[1] - 1) if calendar else (0, 0)
        if precision is None:
            self.inverse_precision = np.zeros((self.m, self.m))
            self.precision = None
        else:
            precision = np.asarray(precision, dtype=float)
            if precision.ndim == 0:
                precision = float(precision) * np.eye(self.m)
            if precision.shape != (self.m, self.m) or not np.isfinite(precision).all():
                raise ValueError("Supply a finite positive definite precision matrix.")
            if not np.allclose(precision, precision.T):
                raise ValueError("Precision must be symmetric.")
            np.linalg.cholesky(precision)
            self.precision = precision
            self.inverse_precision = np.linalg.solve(precision, np.eye(self.m))

    def market(self, q):
        """Apply all target rows to the marginal array, including expected-loss rows if present."""
        return np.einsum("mik,ik->m", self.A, q)

    def adjoint(self, vector):
        """Apply the transpose of the linear target operator."""
        return np.einsum("m,mik->ik", vector, self.A)

    def _state(self, mu, lam):
        """Compute the normalized exponential tilt and stabilize its log normalizers."""
        s = self.adjoint(mu)
        if lam.size:
            s -= calendar_adjoint(lam, self.p.shape)
        logs = self.log_prior - s / self.w[:, None]
        maximum = logs.max(axis=1)
        mass = np.exp(logs - maximum[:, None])
        z = mass.sum(axis=1)
        return mass / z[:, None], maximum + np.log(z)

    def _evaluate(self, mu, lam):
        """Return the negative dual, its target and calendar gradients, and the tilt."""
        q, log_z = self._state(mu, lam)
        residual = self.inverse_precision @ mu
        value = float(mu @ (self.b + .5 * residual) + self.w @ log_z)
        market_gradient = self.b - self.market(q) + residual
        return value, market_gradient, calendar_gaps(q) if lam.size else lam, q

    def _covariance(self, q):
        """Return the target block of the negative-dual Hessian at q."""
        centered = self.A - np.einsum("mik,ik->mi", self.A, q)[:, :, None]
        scale = q / self.w[:, None]
        cov = np.einsum("mik,nik,ik->mn", centered, centered, scale)
        return cov + self.inverse_precision

    def _objective(self, q):
        q = np.maximum(q, 0)
        with np.errstate(divide="ignore", invalid="ignore"):
            entropy = np.sum(np.where(q > 0, q * (np.log(q) - self.log_prior), 0), axis=1)
        value = self.w @ entropy
        if self.precision is not None:
            residual = self.market(q) - self.b
            value += .5 * residual @ self.precision @ residual
        return float(value)

    def solve(self, tolerance=1e-10, max_iterations=40_000, *, initial_mu=None) -> Fit:
        """Fit the dual, optionally starting from given market multipliers."""
        if not np.isfinite(tolerance) or tolerance <= 0 or max_iterations < 1:
            raise ValueError("Supply a positive tolerance and iteration limit.")
        if initial_mu is not None:
            initial_mu = np.asarray(initial_mu, dtype=float)
            if initial_mu.shape != self.b.shape or not np.isfinite(initial_mu).all():
                raise ValueError("Supply one finite initial multiplier per target.")
        if self.calendar_shape == (0, 0):
            return self._newton(tolerance, min(max_iterations, 200), initial_mu)
        return self._with_calendar(max_iterations, initial_mu)

    def _newton(self, tolerance, max_iterations, initial_mu):
        mu = np.zeros(self.m) if initial_mu is None else initial_mu
        lam = np.zeros((0, 0))
        for iteration in range(max_iterations + 1):
            value, gradient, _, q = self._evaluate(mu, lam)
            if _absolute_max(gradient) <= tolerance:
                return Fit(q, mu, lam, iteration, _absolute_max(gradient),
                           max(0., -float(calendar_gaps(q).min(initial=0))), 0.,
                           self._objective(q) + value)
            hessian = self._covariance(q)
            try:
                direction = -np.linalg.solve(hessian, gradient)
            except np.linalg.LinAlgError as exc:
                raise ValueError("Singular market curvature. Check quote rank and feasibility.") from exc
            slope = gradient @ direction
            if not slope < 0:
                raise ValueError("Market solve lost curvature. Check rank and strict feasibility.")
            for step in 0.5 ** np.arange(40):
                trial = mu + step * direction
                if self._evaluate(trial, lam)[0] <= value + 1e-4 * step * slope + 2e-15:
                    mu = trial
                    break
            else:
                raise RuntimeError("Market Newton line search failed.")
        raise RuntimeError("Market Newton solve reached its iteration limit.")

    def _with_calendar(self, max_iterations, initial_mu):
        if initial_mu is None:
            start = EntropicCalibration(self.A, self.b, self.p, self.w, self.precision).solve()
            initial_mu = start.market_multiplier
            q0 = start.q
        else:
            q0, _ = self._state(initial_mu, np.zeros(self.calendar_shape))
        tail = tails(q0)
        variance = tail * (1 - tail) / self.w[:, None]
        quote_scale = 1 / np.sqrt(np.diag(self._covariance(q0)))
        calendar_scale = 1 / np.sqrt(np.maximum(variance[:-1] + variance[1:], .04))
        scale = np.r_[quote_scale, calendar_scale.ravel()]
        x = np.r_[initial_mu, np.zeros(calendar_scale.size)] / scale

        def unpack(x):
            raw = x * scale
            return raw[:self.m], raw[self.m:].reshape(self.calendar_shape)

        def evaluate(x):
            value, market_grad, calendar_grad, _ = self._evaluate(*unpack(x))
            return value, scale * np.r_[market_grad, calendar_grad.ravel()]

        def make_fit(x, iteration):
            mu, lam = unpack(x)
            value, market_grad, gaps, q = self._evaluate(mu, lam)
            feasible = feasible_marginals(q)
            violation = max(0., -float(gaps.min(initial=0)))
            comp = _absolute_max(lam * gaps)
            gap = self._objective(feasible) + value
            stationarity = _absolute_max(market_grad)
            if stationarity <= 4e-8 and violation <= 2e-7 and comp <= 2e-8 and abs(gap) <= 3e-6:
                return Fit(q, mu, lam, iteration, stationarity, violation, comp, gap,
                           _absolute_max(q - feasible))
            return None

        return self._projected_bfgs(x, evaluate, make_fit, max_iterations)

    def _projected_bfgs(self, x, evaluate, make_fit, max_iterations):
        """Projected L-BFGS with Armijo search and primal/dual stopping tests."""
        value, gradient = evaluate(x)
        history = []
        for iteration in range(max_iterations + 1):
            if iteration % 20 == 0:
                fit = make_fit(x, iteration)
                if fit is not None:
                    return fit
            locked = np.r_[np.zeros(self.m, dtype=bool),
                           (x[self.m:] == 0) & (gradient[self.m:] > 0)]
            reduced = np.where(locked, 0, gradient)
            direction = -_inverse_bfgs(reduced, history)
            direction[locked] = 0
            for search_direction in (direction, -reduced):
                for step in 0.5 ** np.arange(32):
                    trial = x + step * search_direction
                    trial[self.m:] = np.maximum(trial[self.m:], 0)
                    delta = trial - x
                    slope = gradient @ delta
                    if slope >= 0:
                        continue
                    trial_value, trial_grad = evaluate(trial)
                    if trial_value <= value + 1e-4 * slope + 2e-15:
                        break
                else:
                    history.clear()
                    continue
                break
            else:
                # A line search can stall at machine precision between scheduled checks.
                fit = make_fit(x, iteration)
                if fit is not None:
                    return fit
                raise RuntimeError("A++ line search stagnated before certification.")
            update = trial_grad - gradient
            curvature = delta @ update
            if curvature > 1e-12 * np.linalg.norm(delta) * np.linalg.norm(update):
                history.append((delta, update, 1 / curvature))
                history = history[-16:]
            x, value, gradient = trial, trial_value, trial_grad
        fit = make_fit(x, max_iterations)
        if fit is None:
            raise RuntimeError(f"A++ did not meet certification after {max_iterations} iterations "
                               f"(scaled projected gradient {_absolute_max(np.where(locked, 0, gradient)):.2g}).")
        return fit

    def refit(self, *, rhs=None, prior=None):
        """Return a calibration with a perturbed target vector or prior on the same grid."""
        return EntropicCalibration(
            self.A, self.b if rhs is None else rhs, self.p if prior is None else prior,
            self.w, self.precision, self.calendar_shape != (0, 0),
        )

    def direction(self, fit: Fit, *, upfront=None, log_prior=None, repricing=None,
                  gap_tolerance=3e-7, multiplier_tolerance=1e-7):
        """Differentiate the raw calibrated tilt by solving the linearized dual KKT system.

        upfront is db in solver units and can include expected-loss targets.
        log_prior is d(log p) with the shape of the marginal grid. repricing is
        dA, with the shape of the target tensor. Date weights, the penalty and the
        calendar operator stay fixed. The answer requires a numerically identifiable,
        locally stable active set; ambiguous calendar rows raise ValueError.
        """
        q, mu, lam = fit.q, fit.market_multiplier, fit.calendar_multiplier
        db = np.zeros_like(self.b) if upfront is None else np.asarray(upfront)
        base = np.zeros_like(q) if log_prior is None else np.asarray(log_prior)
        if (db.shape != self.b.shape or base.shape != q.shape
                or not np.isfinite(db).all() or not np.isfinite(base).all()):
            raise ValueError("Sensitivity directions must be finite and match the targets and prior.")
        direct_market = np.zeros(self.m)
        if repricing is not None:
            dA = np.asarray(repricing)
            base = base - np.einsum("m,mik->ik", mu, dA) / self.w[:, None]
            direct_market = np.einsum("mik,ik->m", dA, q)
        dq_direct = q * (base - (q * base).sum(axis=1, keepdims=True))

        if not lam.size:
            dmu = np.linalg.solve(
                self._covariance(q), self.market(dq_direct) + direct_market - db,
            )
            tilted = -self.adjoint(dmu) / self.w[:, None]
        else:
            gaps = calendar_gaps(q)
            active = lam > multiplier_tolerance
            if np.any(active & (np.abs(gaps) > gap_tolerance)):
                raise ValueError("Active calendar residual is too large for an implicit derivative.")
            if np.any(~active & (gaps <= gap_tolerance)):
                raise ValueError("Weak calendar constraints. Use one-sided recalibration.")
            def pack(market, calendar):
                return np.r_[market, calendar[active]]

            def unpack(vector):
                padded = np.zeros_like(lam)
                padded[active] = vector[self.m:]
                return vector[:self.m], padded

            def hessian(vector):
                u, v = unpack(vector)
                a = (self.adjoint(u) - calendar_adjoint(v, q.shape)) / self.w[:, None]
                dq = q * (a - np.sum(q * a, axis=1, keepdims=True))
                return pack(self.market(dq) + self.inverse_precision @ u, -calendar_gaps(dq))

            rhs = pack(self.market(dq_direct) + direct_market - db, -calendar_gaps(dq_direct))
            tail = tails(q)
            variance = tail * (1 - tail) / self.w[:, None]
            diagonal = pack(np.diag(self._covariance(q)), variance[:-1] + variance[1:])
            derivative = _conjugate_gradient(hessian, rhs, diagonal)
            dmu, dlam = unpack(derivative)
            tilted = -(self.adjoint(dmu) - calendar_adjoint(dlam, q.shape)) / self.w[:, None]
        return dq_direct + q * (tilted - np.sum(q * tilted, axis=1, keepdims=True))


def _inverse_bfgs(vector, history):
    if not history:
        return vector.copy()
    result = vector.copy()
    alpha = []
    for s, y, rho in reversed(history):
        alpha.append(rho * (s @ result))
        result -= alpha[-1] * y
    s, y, _ = history[-1]
    result *= (s @ y) / (y @ y)
    for (s, y, rho), a in zip(history, reversed(alpha)):
        result += s * (a - rho * (y @ result))
    return result


def _conjugate_gradient(operator, rhs, diagonal):
    result, residual = np.zeros_like(rhs), rhs.copy()
    threshold = 1e-11 * max(1., np.linalg.norm(rhs))
    condition = np.maximum(diagonal, 1e-15)
    direction = residual / condition
    product = residual @ direction
    for _ in range(10 * len(rhs)):
        if np.linalg.norm(residual) <= threshold:
            return result
        transformed = operator(direction)
        denom = direction @ transformed
        if denom <= 0:
            break
        step = product / denom
        result += step * direction
        residual -= step * transformed
        preconditioned = residual / condition
        new_product = residual @ preconditioned
        direction = preconditioned + (new_product / product) * direction
        product = new_product
    raise RuntimeError("Active-set sensitivity solve did not converge.")


"""Market inputs, cash-flow kernels and synthetic reference for CDX North America IG 5Y."""
from __future__ import annotations

from calendar import monthrange
from dataclasses import dataclass
from datetime import date, timedelta
from functools import lru_cache
from math import lgamma
import numpy as np


VALUATION = date(2026, 11, 13)
MATURITY = date(2031, 12, 20)
SETTLEMENT = date(2026, 11, 18)
OIS_QUOTES = (
    (1, .0397), (3, .0417), (12, .0451), (24, .0467),
    (36, .0473), (48, .0471), (60, .0470), (84, .0471),
)
ONE_DAY = timedelta(days=1)


def _add_months(day: date, months: int) -> date:
    year, month = divmod(day.year * 12 + day.month - 1 + months, 12)
    return date(year, month + 1, min(day.day, monthrange(year, month + 1)[1]))


def _easter(year: int) -> date:
    a = year % 19
    b, c = divmod(year, 100)
    d, e = divmod(b, 4)
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i, k = divmod(c, 4)
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    month, day = divmod(h + l - 7 * m + 114, 31)
    return date(year, month, day + 1)


def _weekday(year: int, month: int, weekday: int, ordinal: int) -> date:
    if ordinal == -1:
        last = date(year, month, monthrange(year, month)[1])
        return last - timedelta(days=(last.weekday() - weekday) % 7)
    first = date(year, month, 1)
    return first + timedelta(days=(weekday - first.weekday()) % 7 + 7 * (ordinal - 1))


def _observed_us(day: date) -> date:
    return day + timedelta(days={5: -1, 6: 1}.get(day.weekday(), 0))


@lru_cache
def _holidays(year: int, london: bool) -> frozenset[date]:
    easter = _easter(year)
    holidays = {
        _observed_us(date(year, 1, 1)), _observed_us(date(year + 1, 1, 1)),
        _weekday(year, 1, 0, 3), _weekday(year, 2, 0, 3),
        easter - timedelta(days=2), _weekday(year, 5, 0, -1),
        _observed_us(date(year, 6, 19)), _observed_us(date(year, 7, 4)),
        _weekday(year, 9, 0, 1), _weekday(year, 10, 0, 2),
        _observed_us(date(year, 11, 11)), _weekday(year, 11, 3, 4),
        _observed_us(date(year, 12, 25)),
    }
    if london:
        # Joint New York / London payment calendar for CDS cash flows.
        uk = {date(year, 1, 1), date(year, 12, 25), date(year, 12, 26)}
        for holiday in sorted(uk):
            if holiday.weekday() >= 5:
                substitute = holiday + ONE_DAY
                while substitute.weekday() >= 5 or substitute in uk:
                    substitute += ONE_DAY
                uk.add(substitute)
        holidays.update(uk)
        holidays.update({easter + ONE_DAY, _weekday(year, 5, 0, 1),
                         _weekday(year, 5, 0, -1), _weekday(year, 8, 0, -1)})
    return frozenset(holidays)


def business_day(day: date, *, london: bool = False) -> bool:
    return day.weekday() < 5 and day not in _holidays(day.year, london)


def following(day: date, *, london: bool = False) -> date:
    while not business_day(day, london=london):
        day += ONE_DAY
    return day


def advance_business(day: date, days: int, *, london: bool = False) -> date:
    for _ in range(days):
        day = following(day + ONE_DAY, london=london)
    return day


def modified_following(day: date) -> date:
    candidate = following(day)
    if candidate.month != day.month:
        candidate = day
        while not business_day(candidate):
            candidate -= ONE_DAY
    return candidate


def coupon_dates(start: date, end: date) -> tuple[date, ...]:
    dates = tuple(
        date(year, month, 20)
        for year in range(start.year, end.year + 1)
        for month in (3, 6, 9, 12)
        if start < date(year, month, 20) < end
    )
    return dates + (end,)


def _bracketed_root(function, left: float, right: float, tol: float = 1e-13) -> float:
    f_left, f_right = function(left), function(right)
    if f_left * f_right > 0:
        raise ValueError("Root is outside the bracket.")
    if abs(f_left) <= tol:
        return left
    if abs(f_right) <= tol:
        return right
    while right - left > tol:
        mid = (left + right) / 2
        f_mid = function(mid)
        if f_left * f_mid <= 0:
            right = mid
        else:
            left, f_left = mid, f_mid
    return (left + right) / 2


class OisCurve:
    """USD SOFR OIS bootstrap with ACT/360, spot T+2 and payment lag T+2."""

    def __init__(self, valuation: date = VALUATION, quotes=OIS_QUOTES, overnight=.0390):
        self.valuation = valuation
        self.spot = advance_business(valuation, 2)
        self.quotes = tuple(quotes)
        if not self.quotes or any(m <= 0 for m, _ in self.quotes):
            raise ValueError("Provide positive OIS tenors.")
        tenors = np.asarray([months for months, _ in self.quotes])
        if np.any(np.diff(tenors) <= 0) or not np.isfinite(self.quotes).all():
            raise ValueError("OIS tenors must increase and rates must be finite.")
        dates = [valuation, self.spot]
        logs = [0., -np.log1p(overnight * (self.spot - valuation).days / 360)]
        for months, rate in self.quotes:
            accrual_dates, payment_dates, accrual = self._schedule(months)
            pillar = payment_dates[-1]

            def balance(log_terminal):
                knots = dates + [pillar]
                factors = logs + [log_terminal]
                df = self._interpolate(accrual_dates, knots, factors)
                payment_df = self._interpolate(payment_dates, knots, factors)
                return rate * (accrual @ payment_df) - ((df[:-1] / df[1:] - 1) @ payment_df)

            logs.append(_bracketed_root(balance, -15., 3.))
            dates.append(pillar)
        self.dates = tuple(dates)
        self.logs = np.asarray(logs)

    def _schedule(self, months: int):
        offsets = tuple(range(12, months, 12)) + (months,)
        ends = tuple(modified_following(_add_months(self.spot, t)) for t in offsets)
        accrual_dates = (self.spot,) + ends
        payment_dates = tuple(advance_business(day, 2) for day in ends)
        ordinals = np.array([day.toordinal() for day in accrual_dates])
        return accrual_dates, payment_dates, np.diff(ordinals) / 360

    @staticmethod
    def _interpolate(points, dates, logs) -> np.ndarray:
        ordinals = [day.toordinal() for day in points]
        pillars = [day.toordinal() for day in dates]
        if min(ordinals) < pillars[0] or max(ordinals) > pillars[-1]:
            raise ValueError("Requested dates must lie within the OIS curve.")
        return np.exp(np.interp(ordinals, pillars, logs))

    def discount(self, points) -> np.ndarray | float:
        """Return D(0, t) for a date or a vector of dates inside the curve range."""
        if isinstance(points, date):
            return float(self._interpolate((points,), self.dates, self.logs)[0])
        return self._interpolate(points, self.dates, self.logs)

    def par_rate(self, months: int) -> float:
        """Reprice an OIS pillar with the same accrual and payment schedules as the bootstrap."""
        dates, payments, accrual = self._schedule(months)
        df = self.discount(dates)
        payment_df = self.discount(payments)
        return ((df[:-1] / df[1:] - 1) @ payment_df) / (accrual @ payment_df)


@dataclass(frozen=True)
class Tranche:
    """Attachment and detachment as fractions, coupon as an annual fraction."""

    attachment: float
    detachment: float
    coupon: float = .01

    def __post_init__(self):
        if not 0 <= self.attachment < self.detachment <= 1:
            raise ValueError("Tranche boundaries must satisfy 0 <= attachment < detachment <= 1.")
        if not np.isfinite(self.coupon) or self.coupon < 0:
            raise ValueError("Coupon must be finite and nonnegative.")

    @property
    def name(self) -> str:
        return f"{100 * self.attachment:g}-{100 * self.detachment:g} %"

    @classmethod
    def from_percent(cls, attachment, detachment, coupon_bp=100):
        """Create a tranche from attachment/detachment percentages and an annual coupon in bp."""
        return cls(attachment / 100, detachment / 100, coupon_bp / 10_000)

    def payoffs(self, loss: np.ndarray, recovered: np.ndarray):
        """Return statewise loss paid by protection and total notional reduction.

        Inputs are fractions of index notional; outputs are fractions of tranche
        notional. The second output includes recovery amortization and determines
        the outstanding notional used in the premium leg.
        """
        width = self.detachment - self.attachment
        protection = np.clip(loss - self.attachment, 0, width) / width
        recovery_reduction = np.clip(recovered - (1 - self.detachment), 0, width) / width
        return protection, protection + recovery_reduction


@dataclass(frozen=True)
class Legs:
    """Protection PV, risky annuity and coupon per unit of initial instrument notional.

    PV values are at valuation. The upfront property converts their net PV
    to a clean settlement amount. The par spread is an annual fraction.
    """
    protection: float
    annuity: float
    coupon: float
    settlement_discount: float

    @property
    def upfront(self) -> float:
        """Clean settlement amount as a fraction of initial instrument notional."""
        return (self.protection - self.coupon * self.annuity) / self.settlement_discount

    @property
    def par_spread(self) -> float:
        return self.protection / self.annuity


@dataclass(frozen=True)
class PricingKernel:
    """Affine clean-upfront map for marginals on the calibration dates.

    The row has shape (date, default count) and excludes the valuation date.
    Its contraction with q equals the net upfront PV plus the coupon on the
    full-notional annuity. The same constant enters the calibration RHS.
    """
    row: np.ndarray
    full_notional_annuity: float
    coupon: float
    settlement_discount: float

    def rhs(self, upfront: float) -> float:
        """Convert a clean settlement upfront into the target for the pricing-kernel row."""
        return upfront * self.settlement_discount + self.coupon * self.full_notional_annuity

    def upfront(self, q: np.ndarray) -> float:
        """Evaluate a clean settlement upfront from date-by-state marginals."""
        value = np.sum(self.row * q) - self.coupon * self.full_notional_annuity
        return value / self.settlement_discount


class Cashflows:
    """Quarterly calibration and pricing cashflows from thesis Proposition 7.4."""

    def __init__(self, curve: OisCurve, maturity=MATURITY, settlement=SETTLEMENT):
        self.curve, self.maturity, self.settlement = curve, maturity, settlement
        valuation = curve.valuation
        if maturity <= valuation or not valuation <= settlement <= maturity:
            raise ValueError("Check maturity and initial settlement.")
        self.ends = coupon_dates(valuation, maturity)
        elapsed_days = np.array([0, *((day - valuation).days for day in self.ends)])
        self.times = elapsed_days / 365
        self.accruals = np.diff(elapsed_days) / 360
        payments = tuple(following(day, london=True) for day in self.ends)
        self.coupon_discount = curve.discount(payments)
        self.settlement_discount = curve.discount(settlement)

    def reference_legs(self, expected_loss, expected_reduction, coupon=.01) -> Legs:
        """Value the quarterly legs directly from curves including valuation date.

        Protection settles the loss increment at the coupon date. Premium
        accrues on the end-of-period outstanding notional with ACT/360.
        """
        loss, reduction = np.asarray(expected_loss), np.asarray(expected_reduction)
        if loss.shape != self.times.shape or reduction.shape != self.times.shape:
            raise ValueError("Expected one value per grid date, including valuation date.")
        protection = float(self.coupon_discount @ np.diff(loss))
        annuity = float((self.accruals * self.coupon_discount) @ (1 - reduction[1:]))
        return Legs(protection, annuity, coupon, self.settlement_discount)

    def grid(self):
        """Build the quarterly kernel weights on the valuation and coupon dates."""
        discount = self.coupon_discount
        protection = np.r_[-discount[0], discount[:-1] - discount[1:], discount[-1]]
        premium = np.r_[0., self.accruals * discount]
        return ValuationGrid(self.times, protection, premium, self.settlement_discount)


@dataclass(frozen=True)
class ValuationGrid:
    """Times in ACT/365 and cashflow weights, including valuation at time zero."""
    times: np.ndarray
    protection_weights: np.ndarray
    premium_weights: np.ndarray
    settlement_discount: float

    def legs(self, expected_loss, expected_reduction, coupon=.01) -> Legs:
        """Value both legs from expected loss and reduction on the complete grid."""
        protection = float(self.protection_weights @ expected_loss)
        annuity = float(self.premium_weights @ (1 - expected_reduction))
        return Legs(protection, annuity, coupon, self.settlement_discount)

    def kernel(self, tranche: Tranche, portfolio: Portfolio) -> PricingKernel:
        """Build a linear pricing row, omitting the fixed no-default valuation state."""
        loss, reduction = tranche.payoffs(portfolio.loss, portfolio.recovered)
        row = (
            self.protection_weights[1:, None] * loss
            + tranche.coupon * self.premium_weights[1:, None] * reduction
        )
        return PricingKernel(row, float(self.premium_weights.sum()), tranche.coupon,
                             self.settlement_discount)


@dataclass(frozen=True)
class Portfolio:
    """Homogeneous, equally weighted portfolio with deterministic recovery."""
    names: int = 125
    recovery: float = .60

    def __post_init__(self):
        if not isinstance(self.names, int) or self.names < 1 or not 0 <= self.recovery < 1:
            raise ValueError("Require a positive integer number of names and recovery in [0, 1).")

    @property
    def default_fraction(self):
        return np.arange(self.names + 1) / self.names

    @property
    def loss(self):
        return (1 - self.recovery) * self.default_fraction

    @property
    def recovered(self):
        return self.recovery * self.default_fraction


@lru_cache
def _log_combinations(n: int) -> np.ndarray:
    return np.array([lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1) for k in range(n + 1)])


def binomial_probabilities(n: int, probability: np.ndarray) -> np.ndarray:
    p = np.asarray(probability, dtype=float)[..., None]
    if not np.all((0 <= p) & (p <= 1)):
        raise ValueError("Binomial probabilities must lie in [0, 1].")
    k = np.arange(n + 1)
    with np.errstate(divide="ignore", invalid="ignore"):
        successes = np.where(k == 0, 0., k * np.log(p))
        failures = np.where(k == n, 0., (n - k) * np.log1p(-p))
    mass = np.exp(_log_combinations(n) + successes + failures)
    return mass / mass.sum(axis=-1, keepdims=True)


@dataclass(frozen=True)
class CommonShock:
    """Synthetic default-count marginals obtained by mixing two binomial laws."""
    portfolio: Portfolio
    individual_intensity: float = .0075
    shock_intensity: float = .018
    exposure: float = .35

    def survival(self, t):
        t = np.asarray(t, dtype=float)
        no_shock = np.exp(-self.shock_intensity * t)
        return np.exp(-self.individual_intensity * t) * (1 - self.exposure * (1 - no_shock))

    def marginals(self, t):
        t = np.asarray(t, dtype=float)
        p0 = -np.expm1(-self.individual_intensity * t)
        occurred = -np.expm1(-self.shock_intensity * t)
        p1 = p0 + self.exposure * (1 - p0)
        n = self.portfolio.names
        return ((1 - occurred)[..., None] * binomial_probabilities(n, p0)
                + occurred[..., None] * binomial_probabilities(n, p1))

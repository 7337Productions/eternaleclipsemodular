#pragma once
#include <cmath>

// Simplified Keplerian orbital mechanics for the Sephirothic Modulator.
// Elements from JPL "Keplerian Elements for Approximate Positions of the
// Major Planets" (Table 1, nominally 1800-2050). Precision target is
// expressive CV, not ephemeris accuracy.
namespace orbits {

enum Planet {
	MERCURY, VENUS, EMB, MARS, JUPITER, SATURN, URANUS, NEPTUNE, NUM_PLANETS
};

// a [au], e, I [deg], L [deg], longPeri [deg], longNode [deg] + rates/century
struct Elements {
	double a, e, I, L, w, O;
	double ad, ed, Id, Ld, wd, Od;
};

static const Elements ELEMENTS[NUM_PLANETS] = {
	{0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593,
	 0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081},
	{0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255,
	 0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418},
	{1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0,
	 0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0},
	{1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891,
	 0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343},
	{5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909,
	 -0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106},
	{9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448,
	 -0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794},
	{19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503,
	 -0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589},
	{30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574,
	 0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664},
};

static const double DEG = M_PI / 180.0;

// J2000 epoch = unix 946727935.816. Clamped to +-500 centuries so drifting
// elements can never go degenerate (e >= 1) under extreme time warp.
inline double unixToCenturies(double unixTime) {
	double T = (unixTime - 946727935.816) / (86400.0 * 36525.0);
	return T < -500.0 ? -500.0 : (T > 500.0 ? 500.0 : T);
}

// Heliocentric ecliptic position [au], J2000 ecliptic plane projection
inline void heliocentric(int p, double T, double& x, double& y) {
	const Elements& el = ELEMENTS[p];
	double a = el.a + el.ad * T;
	double e = el.e + el.ed * T;
	double I = (el.I + el.Id * T) * DEG;
	double L = (el.L + el.Ld * T) * DEG;
	double w = (el.w + el.wd * T) * DEG;
	double O = (el.O + el.Od * T) * DEG;

	double M = std::fmod(L - w, 2.0 * M_PI);
	// Kepler's equation by Newton iteration
	double E = M + e * std::sin(M);
	for (int i = 0; i < 6; i++)
		E -= (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));

	double xp = a * (std::cos(E) - e);
	double yp = a * std::sqrt(1.0 - e * e) * std::sin(E);

	double argPeri = w - O;
	double cw = std::cos(argPeri), sw = std::sin(argPeri);
	double cO = std::cos(O), sO = std::sin(O);
	double cI = std::cos(I);
	x = (cw * cO - sw * sO * cI) * xp + (-sw * cO - cw * sO * cI) * yp;
	y = (cw * sO + sw * cO * cI) * xp + (-sw * sO + cw * cO * cI) * yp;
}

// Full solar-system snapshot at one instant
struct Sky {
	double hx[NUM_PLANETS], hy[NUM_PLANETS]; // heliocentric [au]
	double gx[NUM_PLANETS], gy[NUM_PLANETS]; // geocentric [au]
	double helioLon[NUM_PLANETS];            // radians
	double geoLon[NUM_PLANETS];              // radians
	double sunLon;                           // geocentric solar longitude, radians
	double moonPhase;                        // synodic phase 0..1 (0 = new)

	void compute(double unixTime) {
		double T = unixToCenturies(unixTime);
		for (int p = 0; p < NUM_PLANETS; p++)
			heliocentric(p, T, hx[p], hy[p]);
		for (int p = 0; p < NUM_PLANETS; p++) {
			gx[p] = hx[p] - hx[EMB];
			gy[p] = hy[p] - hy[EMB];
			helioLon[p] = std::atan2(hy[p], hx[p]);
			geoLon[p] = std::atan2(gy[p], gx[p]);
		}
		sunLon = std::atan2(-hy[EMB], -hx[EMB]);
		// Synodic month, epoch: new moon 2000-01-06 18:14 UTC (unix 947182440)
		double days = (unixTime - 947182440.0) / 86400.0;
		moonPhase = std::fmod(days / 29.53058867, 1.0);
		if (moonPhase < 0.0)
			moonPhase += 1.0;
	}
};

} // namespace orbits

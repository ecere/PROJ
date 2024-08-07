/*
 * This code was entirely written by Nathan Wagner
 * and is in the public domain.
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

#include "proj.h"
#include "proj_internal.h"
#include <math.h>

#define DEG36 0.62831853071795864768
#define DEG72 1.25663706143591729537
#define DEG90 M_PI_2
#define DEG108 1.88495559215387594306
#define DEG120 2.09439510239319549229
#define DEG144 2.51327412287183459075
#define DEG180 M_PI

/* sqrt(5)/M_PI */
#define ISEA_SCALE 0.8301572857837594396028083

/* 26.565051177 degrees */
#define V_LAT 0.46364760899944494524

/* 52.62263186 */
#define E_RAD 0.91843818702186776133

/* 10.81231696 */
#define F_RAD 0.18871053072122403508

/* R tan(g) sin(60) */
#define TABLE_G 0.6615845383

/* H = 0.25 R tan g = */
#define TABLE_H 0.1909830056

/* in radians */
#define ISEA_STD_LAT 1.01722196792335072101
#define ISEA_STD_LONG .19634954084936207740

namespace { // anonymous namespace
struct hex {
    int iso;
    long x, y, z;
};
} // anonymous namespace

/* y *must* be positive down as the xy /iso conversion assumes this */
static void hex_xy(struct hex *h) {
    if (!h->iso)
        return;
    if (h->x >= 0) {
        h->y = -h->y - (h->x + 1) / 2;
    } else {
        /* need to round toward -inf, not toward zero, so x-1 */
        h->y = -h->y - h->x / 2;
    }
    h->iso = 0;
}

static void hex_iso(struct hex *h) {
    if (h->iso)
        return;

    if (h->x >= 0) {
        h->y = (-h->y - (h->x + 1) / 2);
    } else {
        /* need to round toward -inf, not toward zero, so x-1 */
        h->y = (-h->y - (h->x) / 2);
    }

    h->z = -h->x - h->y;
    h->iso = 1;
}

static void hexbin2(double width, double x, double y, long *i, long *j) {
    double z, rx, ry, rz;
    double abs_dx, abs_dy, abs_dz;
    long ix, iy, iz, s;
    struct hex h;

    x = x / cos(30 * M_PI / 180.0); /* rotated X coord */
    y = y - x / 2.0;                /* adjustment for rotated X */

    /* adjust for actual hexwidth */
    if (width == 0) {
        throw "Division by zero";
    }
    x /= width;
    y /= width;

    z = -x - y;

    rx = floor(x + 0.5);
    ix = lround(rx);
    ry = floor(y + 0.5);
    iy = lround(ry);
    rz = floor(z + 0.5);
    iz = lround(rz);
    if (fabs((double)ix + iy) > std::numeric_limits<int>::max() ||
        fabs((double)ix + iy + iz) > std::numeric_limits<int>::max()) {
        throw "Integer overflow";
    }

    s = ix + iy + iz;

    if (s) {
        abs_dx = fabs(rx - x);
        abs_dy = fabs(ry - y);
        abs_dz = fabs(rz - z);

        if (abs_dx >= abs_dy && abs_dx >= abs_dz) {
            ix -= s;
        } else if (abs_dy >= abs_dx && abs_dy >= abs_dz) {
            iy -= s;
        } else {
            iz -= s;
        }
    }
    h.x = ix;
    h.y = iy;
    h.z = iz;
    h.iso = 1;

    hex_xy(&h);
    *i = h.x;
    *j = h.y;
}

namespace { // anonymous namespace
enum isea_poly { ISEA_NONE, ISEA_ICOSAHEDRON = 20 };
enum isea_topology { ISEA_HEXAGON = 6, ISEA_TRIANGLE = 3, ISEA_DIAMOND = 4 };
enum isea_address_form {
    ISEA_GEO,
    ISEA_Q2DI,
    ISEA_SEQNUM,
    ISEA_INTERLEAVE,
    ISEA_PLANE,
    ISEA_Q2DD,
    ISEA_PROJTRI,
    ISEA_VERTEX2DD,
    ISEA_HEX
};
} // anonymous namespace

namespace { // anonymous namespace
struct isea_dgg {
    int polyhedron;            /* ignored, icosahedron */
    double o_lat, o_lon, o_az; /* orientation, radians */
    int topology;              /* ignored, hexagon */
    int aperture;              /* valid values depend on partitioning method */
    int resolution;
    double radius; /* radius of the earth in meters, ignored 1.0 */
    int output;    /* an isea_address_form */
    int triangle;  /* triangle of last transformed point */
    int quad;      /* quad of last transformed point */
    unsigned long serial;
};
} // anonymous namespace

namespace { // anonymous namespace
struct isea_pt {
    double x, y;
};
} // anonymous namespace

namespace { // anonymous namespace
struct isea_geo {
    double longitude, lat;
};
} // anonymous namespace

/* ENDINC */

namespace { // anonymous namespace
enum snyder_polyhedron {
    SNYDER_POLY_HEXAGON,
    SNYDER_POLY_PENTAGON,
    SNYDER_POLY_TETRAHEDRON,
    SNYDER_POLY_CUBE,
    SNYDER_POLY_OCTAHEDRON,
    SNYDER_POLY_DODECAHEDRON,
    SNYDER_POLY_ICOSAHEDRON
};
} // anonymous namespace

namespace { // anonymous namespace
struct snyder_constants {
    double g, G, theta;
    /* cppcheck-suppress unusedStructMember */
    double ea_w, ea_a, ea_b, g_w, g_a, g_b;
};
} // anonymous namespace

/* TODO put these in radians to avoid a later conversion */
static const struct snyder_constants constants[] = {
    {23.80018260, 62.15458023, 60.0, 3.75, 1.033, 0.968, 5.09, 1.195, 1.0},
    {20.07675127, 55.69063953, 54.0, 2.65, 1.030, 0.983, 3.59, 1.141, 1.027},
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {37.37736814, 36.0, 30.0, 17.27, 1.163, 0.860, 13.14, 1.584, 1.0},
};

static struct isea_geo vertex[] = {
    {0.0, DEG90},   {DEG180, V_LAT}, {-DEG108, V_LAT},  {-DEG36, V_LAT},
    {DEG36, V_LAT}, {DEG108, V_LAT}, {-DEG144, -V_LAT}, {-DEG72, -V_LAT},
    {0.0, -V_LAT},  {DEG72, -V_LAT}, {DEG144, -V_LAT},  {0.0, -DEG90}};

/* TODO make an isea_pt array of the vertices as well */

static int tri_v1[] = {0, 0, 0, 0, 0, 0,  6,  7,  8,  9, 10,
                       2, 3, 4, 5, 1, 11, 11, 11, 11, 11};

/* triangle Centers */
static const struct isea_geo icostriangles[] = {
    {0.0, 0.0},        {-DEG144, E_RAD}, {-DEG72, E_RAD},  {0.0, E_RAD},
    {DEG72, E_RAD},    {DEG144, E_RAD},  {-DEG144, F_RAD}, {-DEG72, F_RAD},
    {0.0, F_RAD},      {DEG72, F_RAD},   {DEG144, F_RAD},  {-DEG108, -F_RAD},
    {-DEG36, -F_RAD},  {DEG36, -F_RAD},  {DEG108, -F_RAD}, {DEG180, -F_RAD},
    {-DEG108, -E_RAD}, {-DEG36, -E_RAD}, {DEG36, -E_RAD},  {DEG108, -E_RAD},
    {DEG180, -E_RAD},
};

static double az_adjustment(int triangle) {
    double adj;

    struct isea_geo v;
    struct isea_geo c;

    v = vertex[tri_v1[triangle]];
    c = icostriangles[triangle];

    /* TODO looks like the adjustment is always either 0 or 180 */
    /* at least if you pick your vertex carefully */
    adj = atan2(cos(v.lat) * sin(v.longitude - c.longitude),
                cos(c.lat) * sin(v.lat) -
                    sin(c.lat) * cos(v.lat) * cos(v.longitude - c.longitude));
    return adj;
}

static struct isea_pt isea_triangle_xy(int triangle) {
    struct isea_pt c;
    const double Rprime = 0.91038328153090290025;

    triangle = (triangle - 1) % 20;

    c.x = TABLE_G * ((triangle % 5) - 2) * 2.0;
    if (triangle > 9) {
        c.x += TABLE_G;
    }
    switch (triangle / 5) {
    case 0:
        c.y = 5.0 * TABLE_H;
        break;
    case 1:
        c.y = TABLE_H;
        break;
    case 2:
        c.y = -TABLE_H;
        break;
    case 3:
        c.y = -5.0 * TABLE_H;
        break;
    default:
        /* should be impossible */
        exit(EXIT_FAILURE);
    }
    c.x *= Rprime;
    c.y *= Rprime;

    return c;
}

/* snyder eq 14 */
static double sph_azimuth(double f_lon, double f_lat, double t_lon,
                          double t_lat) {
    double az;

    az = atan2(cos(t_lat) * sin(t_lon - f_lon),
               cos(f_lat) * sin(t_lat) -
                   sin(f_lat) * cos(t_lat) * cos(t_lon - f_lon));
    return az;
}

#ifdef _MSC_VER
#pragma warning(push)
/* disable unreachable code warning for return 0 */
#pragma warning(disable : 4702)
#endif

/* coord needs to be in radians */
static int isea_snyder_forward(struct isea_geo *ll, struct isea_pt *out) {
    int i;

    /*
     * spherical distance from center of polygon face to any of its
     * vertices on the globe
     */
    double g;

    /*
     * spherical angle between radius vector to center and adjacent edge
     * of spherical polygon on the globe
     */
    double G;

    /*
     * plane angle between radius vector to center and adjacent edge of
     * plane polygon
     */
    double theta;

    /* additional variables from snyder */
    double q, H, Ag, Azprime, Az, dprime, f, rho, x, y;

    /* variables used to store intermediate results */
    double cot_theta, tan_g, az_offset;

    /* how many multiples of 60 degrees we adjust the azimuth */
    int Az_adjust_multiples;

    struct snyder_constants c;

    /*
     * TODO by locality of reference, start by trying the same triangle
     * as last time
     */

    /* TODO put these constants in as radians to begin with */
    c = constants[SNYDER_POLY_ICOSAHEDRON];
    theta = PJ_TORAD(c.theta);
    g = PJ_TORAD(c.g);
    G = PJ_TORAD(c.G);

    for (i = 1; i <= 20; i++) {
        double z;
        struct isea_geo center;

        center = icostriangles[i];

        /* step 1 */
        z = acos(sin(center.lat) * sin(ll->lat) +
                 cos(center.lat) * cos(ll->lat) *
                     cos(ll->longitude - center.longitude));
        /* not on this triangle */
        if (z > g + 0.000005) { /* TODO DBL_EPSILON */
            continue;
        }

        Az = sph_azimuth(center.longitude, center.lat, ll->longitude, ll->lat);

        /* step 2 */

        /* This calculates "some" vertex coordinate */
        az_offset = az_adjustment(i);

        Az -= az_offset;

        /* TODO I don't know why we do this.  It's not in snyder */
        /* maybe because we should have picked a better vertex */
        if (Az < 0.0) {
            Az += 2.0 * M_PI;
        }
        /*
         * adjust Az for the point to fall within the range of 0 to
         * 2(90 - theta) or 60 degrees for the hexagon, by
         * and therefore 120 degrees for the triangle
         * of the icosahedron
         * subtracting or adding multiples of 60 degrees to Az and
         * recording the amount of adjustment
         */

        Az_adjust_multiples = 0;
        while (Az < 0.0) {
            Az += DEG120;
            Az_adjust_multiples--;
        }
        while (Az > DEG120 + DBL_EPSILON) {
            Az -= DEG120;
            Az_adjust_multiples++;
        }

        /* step 3 */
        cot_theta = 1.0 / tan(theta);
        tan_g = tan(g); /* TODO this is a constant */

        /* Calculate q from eq 9. */
        /* TODO cot_theta is cot(30) */
        q = atan2(tan_g, cos(Az) + sin(Az) * cot_theta);

        /* not in this triangle */
        if (z > q + 0.000005) {
            continue;
        }
        /* step 4 */

        /* Apply equations 5-8 and 10-12 in order */

        /* eq 5 */
        /* Rprime = 0.9449322893 * R; */
        /* R' in the paper is for the truncated */
        const double Rprime = 0.91038328153090290025;

        /* eq 6 */
        H = acos(sin(Az) * sin(G) * cos(g) - cos(Az) * cos(G));

        /* eq 7 */
        /* Ag = (Az + G + H - DEG180) * M_PI * R * R / DEG180; */
        Ag = Az + G + H - DEG180;

        /* eq 8 */
        Azprime = atan2(2.0 * Ag,
                        Rprime * Rprime * tan_g * tan_g - 2.0 * Ag * cot_theta);

        /* eq 10 */
        /* cot(theta) = 1.73205080756887729355 */
        dprime = Rprime * tan_g / (cos(Azprime) + sin(Azprime) * cot_theta);

        /* eq 11 */
        f = dprime / (2.0 * Rprime * sin(q / 2.0));

        /* eq 12 */
        rho = 2.0 * Rprime * f * sin(z / 2.0);

        /*
         * add back the same 60 degree multiple adjustment from step
         * 2 to Azprime
         */

        Azprime += DEG120 * Az_adjust_multiples;

        /* calculate rectangular coordinates */

        x = rho * sin(Azprime);
        y = rho * cos(Azprime);

        /*
         * TODO
         * translate coordinates to the origin for the particular
         * hexagon on the flattened polyhedral map plot
         */

        out->x = x;
        out->y = y;

        return i;
    }

    /*
     * should be impossible, this implies that the coordinate is not on
     * any triangle
     */

    fprintf(stderr, "impossible transform: %f %f is not on any triangle\n",
            PJ_TODEG(ll->longitude), PJ_TODEG(ll->lat));

    exit(EXIT_FAILURE);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/*
 * return the new coordinates of any point in original coordinate system.
 * Define a point (newNPold) in original coordinate system as the North Pole in
 * new coordinate system, and the great circle connect the original and new
 * North Pole as the lon0 longitude in new coordinate system, given any point
 * in original coordinate system, this function return the new coordinates.
 */

/* formula from Snyder, Map Projections: A working manual, p31 */
/*
 * old north pole at np in new coordinates
 * could be simplified a bit with fewer intermediates
 *
 * TODO take a result pointer
 */
static struct isea_geo snyder_ctran(struct isea_geo *np, struct isea_geo *pt) {
    struct isea_geo npt;
    double alpha, phi, lambda, lambda0, beta, lambdap, phip;
    double sin_phip;
    double lp_b; /* lambda prime minus beta */
    double cos_p, sin_a;

    phi = pt->lat;
    lambda = pt->longitude;
    alpha = np->lat;
    beta = np->longitude;
    lambda0 = beta;

    cos_p = cos(phi);
    sin_a = sin(alpha);

    /* mpawm 5-7 */
    sin_phip = sin_a * sin(phi) - cos(alpha) * cos_p * cos(lambda - lambda0);

    /* mpawm 5-8b */

    /* use the two argument form so we end up in the right quadrant */
    lp_b =
        atan2(cos_p * sin(lambda - lambda0),
              (sin_a * cos_p * cos(lambda - lambda0) + cos(alpha) * sin(phi)));

    lambdap = lp_b + beta;

    /* normalize longitude */
    /* TODO can we just do a modulus ? */
    lambdap = fmod(lambdap, 2 * M_PI);
    while (lambdap > M_PI)
        lambdap -= 2 * M_PI;
    while (lambdap < -M_PI)
        lambdap += 2 * M_PI;

    phip = asin(sin_phip);

    npt.lat = phip;
    npt.longitude = lambdap;

    return npt;
}

static struct isea_geo isea_ctran(struct isea_geo *np, struct isea_geo *pt,
                                  double lon0) {
    struct isea_geo npt;

    np->longitude += M_PI;
    npt = snyder_ctran(np, pt);
    np->longitude -= M_PI;

    npt.longitude -= (M_PI - lon0 + np->longitude);

    /*
     * snyder is down tri 3, isea is along side of tri1 from vertex 0 to
     * vertex 1 these are 180 degrees apart
     */
    npt.longitude += M_PI;
    /* normalize longitude */
    npt.longitude = fmod(npt.longitude, 2 * M_PI);
    while (npt.longitude > M_PI)
        npt.longitude -= 2 * M_PI;
    while (npt.longitude < -M_PI)
        npt.longitude += 2 * M_PI;

    return npt;
}

/* fuller's at 5.2454 west, 2.3009 N, adjacent at 7.46658 deg */

static int isea_grid_init(struct isea_dgg *g) {
    if (!g)
        return 0;

    g->polyhedron = 20;
    g->o_lat = ISEA_STD_LAT;
    g->o_lon = ISEA_STD_LONG;
    g->o_az = 0.0;
    g->aperture = 4;
    g->resolution = 6;
    g->radius = 1.0;
    g->topology = 6;

    return 1;
}

static void isea_orient_isea(struct isea_dgg *g) {
    if (!g)
        return;
    g->o_lat = ISEA_STD_LAT;
    g->o_lon = ISEA_STD_LONG;
    g->o_az = 0.0;
}

static void isea_orient_pole(struct isea_dgg *g) {
    if (!g)
        return;
    g->o_lat = M_PI / 2.0;
    g->o_lon = 0.0;
    g->o_az = 0;
}

static int isea_transform(struct isea_dgg *g, struct isea_geo *in,
                          struct isea_pt *out) {
    struct isea_geo i, pole;
    int tri;

    pole.lat = g->o_lat;
    pole.longitude = g->o_lon;

    i = isea_ctran(&pole, in, g->o_az);

    tri = isea_snyder_forward(&i, out);
    out->x *= g->radius;
    out->y *= g->radius;
    g->triangle = tri;

    return tri;
}

#define DOWNTRI(tri) (((tri - 1) / 5) % 2 == 1)

static void isea_rotate(struct isea_pt *pt, double degrees) {
    double rad;

    double x, y;

    rad = -degrees * M_PI / 180.0;
    while (rad >= 2.0 * M_PI)
        rad -= 2.0 * M_PI;
    while (rad <= -2.0 * M_PI)
        rad += 2.0 * M_PI;

    x = pt->x * cos(rad) + pt->y * sin(rad);
    y = -pt->x * sin(rad) + pt->y * cos(rad);

    pt->x = x;
    pt->y = y;
}

static int isea_tri_plane(int tri, struct isea_pt *pt, double radius) {
    struct isea_pt tc; /* center of triangle */

    if (DOWNTRI(tri)) {
        isea_rotate(pt, 180.0);
    }
    tc = isea_triangle_xy(tri);
    tc.x *= radius;
    tc.y *= radius;
    pt->x += tc.x;
    pt->y += tc.y;

    return tri;
}

/* convert projected triangle coords to quad xy coords, return quad number */
static int isea_ptdd(int tri, struct isea_pt *pt) {
    int downtri, quadz;

    downtri = (((tri - 1) / 5) % 2 == 1);
    quadz = ((tri - 1) % 5) + ((tri - 1) / 10) * 5 + 1;

    isea_rotate(pt, downtri ? 240.0 : 60.0);
    if (downtri) {
        pt->x += 0.5;
        /* pt->y += cos(30.0 * M_PI / 180.0); */
        pt->y += .86602540378443864672;
    }
    return quadz;
}

static int isea_dddi_ap3odd(struct isea_dgg *g, int quadz, struct isea_pt *pt,
                            struct isea_pt *di) {
    struct isea_pt v;
    double hexwidth;
    double sidelength; /* in hexes */
    long d, i;
    long maxcoord;
    struct hex h;

    /* This is the number of hexes from apex to base of a triangle */
    sidelength = (pow(2.0, g->resolution) + 1.0) / 2.0;

    /* apex to base is cos(30deg) */
    hexwidth = cos(M_PI / 6.0) / sidelength;

    /* TODO I think sidelength is always x.5, so
     * (int)sidelength * 2 + 1 might be just as good
     */
    maxcoord = lround((sidelength * 2.0));

    v = *pt;
    hexbin2(hexwidth, v.x, v.y, &h.x, &h.y);
    h.iso = 0;
    hex_iso(&h);

    d = h.x - h.z;
    i = h.x + h.y + h.y;

    /*
     * you want to test for max coords for the next quad in the same
     * "row" first to get the case where both are max
     */
    if (quadz <= 5) {
        if (d == 0 && i == maxcoord) {
            /* north pole */
            quadz = 0;
            d = 0;
            i = 0;
        } else if (i == maxcoord) {
            /* upper right in next quad */
            quadz += 1;
            if (quadz == 6)
                quadz = 1;
            i = maxcoord - d;
            d = 0;
        } else if (d == maxcoord) {
            /* lower right in quad to lower right */
            quadz += 5;
            d = 0;
        }
    } else /* if (quadz >= 6) */ {
        if (i == 0 && d == maxcoord) {
            /* south pole */
            quadz = 11;
            d = 0;
            i = 0;
        } else if (d == maxcoord) {
            /* lower right in next quad */
            quadz += 1;
            if (quadz == 11)
                quadz = 6;
            d = maxcoord - i;
            i = 0;
        } else if (i == maxcoord) {
            /* upper right in quad to upper right */
            quadz = (quadz - 4) % 5;
            i = 0;
        }
    }

    di->x = d;
    di->y = i;

    g->quad = quadz;
    return quadz;
}

static int isea_dddi(struct isea_dgg *g, int quadz, struct isea_pt *pt,
                     struct isea_pt *di) {
    struct isea_pt v;
    double hexwidth;
    long sidelength; /* in hexes */
    struct hex h;

    if (g->aperture == 3 && g->resolution % 2 != 0) {
        return isea_dddi_ap3odd(g, quadz, pt, di);
    }
    /* todo might want to do this as an iterated loop */
    if (g->aperture > 0) {
        double sidelengthDouble = pow(g->aperture, g->resolution / 2.0);
        if (fabs(sidelengthDouble) > std::numeric_limits<int>::max()) {
            throw "Integer overflow";
        }
        sidelength = lround(sidelengthDouble);
    } else {
        sidelength = g->resolution;
    }

    if (sidelength == 0) {
        throw "Division by zero";
    }
    hexwidth = 1.0 / sidelength;

    v = *pt;
    isea_rotate(&v, -30.0);
    hexbin2(hexwidth, v.x, v.y, &h.x, &h.y);
    h.iso = 0;
    hex_iso(&h);

    /* we may actually be on another quad */
    if (quadz <= 5) {
        if (h.x == 0 && h.z == -sidelength) {
            /* north pole */
            quadz = 0;
            h.z = 0;
            h.y = 0;
            h.x = 0;
        } else if (h.z == -sidelength) {
            quadz = quadz + 1;
            if (quadz == 6)
                quadz = 1;
            h.y = sidelength - h.x;
            h.z = h.x - sidelength;
            h.x = 0;
        } else if (h.x == sidelength) {
            quadz += 5;
            h.y = -h.z;
            h.x = 0;
        }
    } else /* if (quadz >= 6) */ {
        if (h.z == 0 && h.x == sidelength) {
            /* south pole */
            quadz = 11;
            h.x = 0;
            h.y = 0;
            h.z = 0;
        } else if (h.x == sidelength) {
            quadz = quadz + 1;
            if (quadz == 11)
                quadz = 6;
            h.x = h.y + sidelength;
            h.y = 0;
            h.z = -h.x;
        } else if (h.y == -sidelength) {
            quadz -= 4;
            h.y = 0;
            h.z = -h.x;
        }
    }
    di->x = h.x;
    di->y = -h.z;

    g->quad = quadz;
    return quadz;
}

static int isea_ptdi(struct isea_dgg *g, int tri, struct isea_pt *pt,
                     struct isea_pt *di) {
    struct isea_pt v;
    int quadz;

    v = *pt;
    quadz = isea_ptdd(tri, &v);
    quadz = isea_dddi(g, quadz, &v, di);
    return quadz;
}

/* q2di to seqnum */

static long isea_disn(struct isea_dgg *g, int quadz, struct isea_pt *di) {
    long sidelength;
    long sn, height;
    long hexes;

    if (quadz == 0) {
        g->serial = 1;
        return g->serial;
    }
    /* hexes in a quad */
    hexes = lround(pow(static_cast<double>(g->aperture),
                       static_cast<double>(g->resolution)));
    if (quadz == 11) {
        g->serial = 1 + 10 * hexes + 1;
        return g->serial;
    }
    if (g->aperture == 3 && g->resolution % 2 == 1) {
        height = lround(floor((pow(g->aperture, (g->resolution - 1) / 2.0))));
        sn = ((long)di->x) * height;
        sn += ((long)di->y) / height;
        sn += (quadz - 1) * hexes;
        sn += 2;
    } else {
        sidelength = lround((pow(g->aperture, g->resolution / 2.0)));
        sn = lround(
            floor(((quadz - 1) * hexes + sidelength * di->x + di->y + 2)));
    }

    g->serial = sn;
    return sn;
}

/* TODO just encode the quad in the d or i coordinate
 * quad is 0-11, which can be four bits.
 * d' = d << 4 + q, d = d' >> 4, q = d' & 0xf
 */
/* convert a q2di to global hex coord */
static int isea_hex(struct isea_dgg *g, int tri, struct isea_pt *pt,
                    struct isea_pt *hex) {
    struct isea_pt v;
#ifdef FIXME
    long sidelength;
    long d, i, x, y;
#endif
    int quadz;

    quadz = isea_ptdi(g, tri, pt, &v);

    if (v.x < (INT_MIN >> 4) || v.x > (INT_MAX >> 4)) {
        throw "Invalid shift";
    }
    hex->x = ((int)v.x * 16) + quadz;
    hex->y = v.y;

    return 1;
#ifdef FIXME
    d = lround(floor(v.x));
    i = lround(floor(v.y));

    /* Aperture 3 odd resolutions */
    if (g->aperture == 3 && g->resolution % 2 != 0) {
        long offset = lround((pow(3.0, g->resolution - 1) + 0.5));

        d += offset * ((g->quadz - 1) % 5);
        i += offset * ((g->quadz - 1) % 5);

        if (quadz == 0) {
            d = 0;
            i = offset;
        } else if (quadz == 11) {
            d = 2 * offset;
            i = 0;
        } else if (quadz > 5) {
            d += offset;
        }

        x = (2 * d - i) / 3;
        y = (2 * i - d) / 3;

        hex->x = x + offset / 3;
        hex->y = y + 2 * offset / 3;
        return 1;
    }

    /* aperture 3 even resolutions and aperture 4 */
    sidelength = lround((pow(g->aperture, g->resolution / 2.0)));
    if (g->quad == 0) {
        hex->x = 0;
        hex->y = sidelength;
    } else if (g->quad == 11) {
        hex->x = sidelength * 2;
        hex->y = 0;
    } else {
        hex->x = d + sidelength * ((g->quad - 1) % 5);
        if (g->quad > 5)
            hex->x += sidelength;
        hex->y = i + sidelength * ((g->quad - 1) % 5);
    }

    return 1;
#endif
}

static struct isea_pt isea_forward(struct isea_dgg *g, struct isea_geo *in) {
    int tri;
    struct isea_pt out, coord;

    tri = isea_transform(g, in, &out);

    if (g->output == ISEA_PLANE) {
        isea_tri_plane(tri, &out, g->radius);
        return out;
    }

    /* convert to isea standard triangle size */
    out.x = out.x / g->radius * ISEA_SCALE;
    out.y = out.y / g->radius * ISEA_SCALE;
    out.x += 0.5;
    out.y += 2.0 * .14433756729740644112;

    switch (g->output) {
    case ISEA_PROJTRI:
        /* nothing to do, already in projected triangle */
        break;
    case ISEA_VERTEX2DD:
        g->quad = isea_ptdd(tri, &out);
        break;
    case ISEA_Q2DD:
        /* Same as above, we just don't print as much */
        g->quad = isea_ptdd(tri, &out);
        break;
    case ISEA_Q2DI:
        g->quad = isea_ptdi(g, tri, &out, &coord);
        return coord;
        break;
    case ISEA_SEQNUM:
        isea_ptdi(g, tri, &out, &coord);
        /* disn will set g->serial */
        isea_disn(g, g->quad, &coord);
        return coord;
        break;
    case ISEA_HEX:
        isea_hex(g, tri, &out, &coord);
        return coord;
        break;
    }

    return out;
}

/*
 * Proj 4 integration code follows
 */

PROJ_HEAD(isea, "Icosahedral Snyder Equal Area") "\n\tSph";

namespace { // anonymous namespace
struct pj_isea_data {
    struct isea_dgg dgg;
};
} // anonymous namespace

static PJ_XY isea_s_forward(PJ_LP lp, PJ *P) { /* Spheroidal, forward */
    PJ_XY xy = {0.0, 0.0};
    struct pj_isea_data *Q = static_cast<struct pj_isea_data *>(P->opaque);
    struct isea_pt out;
    struct isea_geo in;

    in.longitude = lp.lam;
    in.lat = lp.phi;

    try {
        out = isea_forward(&Q->dgg, &in);
    } catch (const char *) {
        proj_errno_set(P, PROJ_ERR_COORD_TRANSFM_OUTSIDE_PROJECTION_DOMAIN);
        return proj_coord_error().xy;
    }

    xy.x = out.x;
    xy.y = out.y;

    return xy;
}

static PJ_LP isea_s_inverse(PJ_XY xy, PJ *P);

PJ *PJ_PROJECTION(isea) {
    char *opt;
    struct pj_isea_data *Q = static_cast<struct pj_isea_data *>(
        calloc(1, sizeof(struct pj_isea_data)));
    if (nullptr == Q)
        return pj_default_destructor(P, PROJ_ERR_OTHER /*ENOMEM*/);
    P->opaque = Q;

    // NOTE: if a inverse was needed, there is some material at
    // https://brsr.github.io/2021/08/31/snyder-equal-area.html
    P->fwd = isea_s_forward;
    P->inv = isea_s_inverse;
    isea_grid_init(&Q->dgg);

    Q->dgg.output = ISEA_PLANE;
    /*      P->dgg.radius = P->a; / * otherwise defaults to 1 */
    /* calling library will scale, I think */

    opt = pj_param(P->ctx, P->params, "sorient").s;
    if (opt) {
        if (!strcmp(opt, "isea")) {
            isea_orient_isea(&Q->dgg);
        } else if (!strcmp(opt, "pole")) {
            isea_orient_pole(&Q->dgg);
        } else {
            proj_log_error(
                P,
                _("Invalid value for orient: only isea or pole are supported"));
            return pj_default_destructor(P,
                                         PROJ_ERR_INVALID_OP_ILLEGAL_ARG_VALUE);
        }
    }

    if (pj_param(P->ctx, P->params, "tazi").i) {
        Q->dgg.o_az = pj_param(P->ctx, P->params, "razi").f;
    }

    if (pj_param(P->ctx, P->params, "tlon_0").i) {
        Q->dgg.o_lon = pj_param(P->ctx, P->params, "rlon_0").f;
    }

    if (pj_param(P->ctx, P->params, "tlat_0").i) {
        Q->dgg.o_lat = pj_param(P->ctx, P->params, "rlat_0").f;
    }

    opt = pj_param(P->ctx, P->params, "smode").s;
    if (opt) {
        if (!strcmp(opt, "plane")) {
            Q->dgg.output = ISEA_PLANE;
        } else if (!strcmp(opt, "di")) {
            Q->dgg.output = ISEA_Q2DI;
        } else if (!strcmp(opt, "dd")) {
            Q->dgg.output = ISEA_Q2DD;
        } else if (!strcmp(opt, "hex")) {
            Q->dgg.output = ISEA_HEX;
        } else {
            proj_log_error(P, _("Invalid value for mode: only plane, di, dd or "
                                "hex are supported"));
            return pj_default_destructor(P,
                                         PROJ_ERR_INVALID_OP_ILLEGAL_ARG_VALUE);
        }
    }

    if (pj_param(P->ctx, P->params, "trescale").i) {
        Q->dgg.radius = ISEA_SCALE;
    }

    if (pj_param(P->ctx, P->params, "tresolution").i) {
        Q->dgg.resolution = pj_param(P->ctx, P->params, "iresolution").i;
    } else {
        Q->dgg.resolution = 4;
    }

    if (pj_param(P->ctx, P->params, "taperture").i) {
        Q->dgg.aperture = pj_param(P->ctx, P->params, "iaperture").i;
    } else {
        Q->dgg.aperture = 3;
    }

    return P;
}

#undef DEG36
#undef DEG72
#undef DEG90
#undef DEG108
#undef DEG120
#undef DEG144
#undef DEG180
#undef ISEA_SCALE
#undef V_LAT
#undef E_RAD
#undef F_RAD
#undef TABLE_G
#undef TABLE_H
// #undef ISEA_STD_LAT
// #undef ISEA_STD_LONG

/*
   Icosahedron Snyder equal-area (ISEA) Inverse Projection
   --------------------------------------------------------------------------
   This inverse projection was adapted from Java and eC by Jérôme Jacovella-St-Louis,
   originally from the Franz-Benjamin Mocnik's ISEA implementation found at
   https://github.com/mocnik-science/geogrid/blob/master/
    src/main/java/org/giscience/utils/geogrid/projections/ISEAProjection.java
   with the following license:

   MIT License

   Copyright (c) 2017-2019 Heidelberg University

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/* The ISEA projection a projects a sphere on the icosahedron. Thereby the size of areas mapped to the icosahedron
 * are preserved. Angles and distances are however slightly distorted. The angular distortion is below 17.27 degree, and
 * the scale variation is less than 16.3 per cent.
 *
 * The projection has been proposed and has been described in detail by:
 *
 * John P. Snyder: An equal-area map projection for polyhedral globes. Cartographica, 29(1), 10–21, 1992.
 * doi:10.3138/27H7-8K88-4882-1752
 *
 * Another description and improvements can be found in:
 *
 * Erika Harrison, Ali Mahdavi-Amiri, and Faramarz Samavati: Optimization of inverse Snyder polyhedral projection.
 * International Conference on Cyberworlds 2011. doi:10.1109/CW.2011.36
 *
 * Erika Harrison, Ali Mahdavi-Amiri, and Faramarz Samavati: Analysis of inverse Snyder optimizations.
 * In: Marina L. Gavrilova, and C. J. Kenneth Tan (Eds): Transactions on Computational Science XVI. Heidelberg,
 * Springer, 2012. pp. 134–148. doi:10.1007/978-3-642-32663-9_8
 */

struct GeoPoint { double lat, lon; };        // In radians

#define Pi                 3.1415926535897932384626433832795028841971

#define Degrees(x)         ((x) * Pi / 180)

#define wgs84InvFlattening 298.257223563
#define wgs84Major         6378137.0  // Meters
#define wgs84Minor         (wgs84Major - (wgs84Major / wgs84InvFlattening)) // 6356752.3142451792955399

// #define phi                ((1 + sqrt(5)) / 2)
// radius
// #define a2                 (wgs84Major * wgs84Major)
// #define c2                 (wgs84Minor * wgs84Minor)

// #define eccentricity       0.0818191908426 // sqrt(1.0 - (c2/a2));
// #define log1pe_1me         0.1640050079196   // log((1+e)/(1-e)))
// #define S                  (Pi * (2 * a2 + c2/eccentricity * log1pe_1me))
#define earthAuthalicRadius 6371007.18091875 //sqrt(S / (4 * Pi)); // R -- authalic sphere radius for WGS84 [6371007.1809184747 m]
#define R2                 (earthAuthalicRadius * earthAuthalicRadius) // R^2
#define RprimeOverR        0.9103832815095032 // (1 / (2 * sqrt(5)) + 1 / 6.0) * sqrt(Pi * sqrt(3)); // R' / R
#define Rprime             (RprimeOverR * earthAuthalicRadius) // R'

#include <algorithm>

#define Min std::min
#define Max std::max

#define inf std::numeric_limits<double>::infinity()

// distortion
// static double maximumAngularDistortion = 17.27;
// static double maximumScaleVariation = 1.163;
// static double minimumScaleVariation = .860;

// Vertices of dodecahedron centered in icosahedron faces
#define E Degrees(52.6226318593487) //(Degrees)atan((3 + sqrt(5)) / 4); // Latitude of center of top icosahedron faces
#define F Degrees(10.8123169635739) //(Degrees)atan((3 - sqrt(5)) / 4); // Latitude of center of faces mirroring top icosahedron face
#define numIcosahedronFaces   20
static const GeoPoint facesCenterDodecahedronVertices[numIcosahedronFaces] =
{
   {  E, Degrees(-144) }, {  E, Degrees(-72) }, {  E, Degrees( 0) }, {  E, Degrees( 72) }, {  E, Degrees(144) },
   {  F, Degrees(-144) }, {  F, Degrees(-72) }, {  F, Degrees( 0) }, {  F, Degrees( 72) }, {  F, Degrees(144) },
   { -F, Degrees(-108) }, { -F, Degrees(-36) }, { -F, Degrees(36) }, { -F, Degrees(108) }, { -F, Degrees(180) },
   { -E, Degrees(-108) }, { -E, Degrees(-36) }, { -E, Degrees(36) }, { -E, Degrees(108) }, { -E, Degrees(180) }
};
// static define precision = Degrees(1e-9);
#define precision                Degrees(1e-11)
#define precisionPerDefinition   Degrees(1e-5)

#define Rprime2X        (2 * Rprime)
#define AzMax           Degrees(120)
#define sdc2vos         (F + 2 * Degrees(58.2825255885418) /*(Degrees)atan(phi)*/ - Degrees(90)) // g -- sphericalDistanceFromCenterToVerticesOnSphere
#define tang            0.763932022500419 //tan(sdc2vos)
#define cotTheta        (1.0 / tan(Degrees(30)))
#define RprimeTang      (Rprime * tang) // twice the center-to-base distance
#define centerToBase    (RprimeTang / 2)
#define triWidth        (RprimeTang * 1.73205080756887729352744634150587236694280525381038) //sqrt(3)
#define Rprime2Tan2g    (RprimeTang * RprimeTang)
#define cosG            cos(Degrees(36))
#define sinGcosSDC2VoS  sin(Degrees(36)) * cos(sdc2vos) // sin G cos g
#define westVertexLon   Degrees(-144)

static const double yOffsets[4] = { -2*centerToBase, -4*centerToBase, -5*centerToBase, -7*centerToBase };

static inline double latGeocentricToGeodetic(double theta)
{
   static const double a2ob2 = (wgs84Major * wgs84Major) / (wgs84Minor * wgs84Minor);
   return atan(tan(theta) * a2ob2);
}

struct ISEAFacePoint
{
   int face;
   double x, y;
};

class ISEAPlanarProjection
{
public:
   ISEAPlanarProjection(const GeoPoint & value) :
      orientation(value),
      cosOrientationLat(cos(value.lat)),
      sinOrientationLat(sin(value.lat))
   {
   }

   bool cartesianToGeo(const PJ_XY & position, GeoPoint & result)
   {
      bool r = false;
      static const double epsilon = 2E-8; // NOTE: 1E-11 seems too small for forward projection precision at boundaries
      int face = 0;
      // Rotate and shear to determine face if not stored in position.z
      static const double sr = -0.86602540378443864676372317075293618347140262690519, cr = 0.5;    // sin(-60), cos(-60)
      static const double shearX = 1.0 / 1.73205080756887729352744634150587236694280525381038; //sqrt(3); // 0.5773502691896257 -- 2*centerToBase / triWidth
      static const double sx = 1.0 / triWidth, sy = 1.0 / (3*centerToBase);
      double yp = -(position.x * sr + position.y * cr);
      double x = (position.x * cr - position.y * sr + yp * shearX) * sx;
      double y = yp * sy;

           if(x < 0 || (y > x && x < 5 - epsilon)) x += epsilon;
      else if(x > 5 || (y < x && x > 0 + epsilon)) x -= epsilon;

      if(y < 0 || (x > y && y < 6 - epsilon)) y += epsilon;
      else if(y > 6 || (x < y && y > 0 + epsilon)) y -= epsilon;

      if(x >= 0 && x <= 5 && y >= 0 && y <= 6)
      {
         int ix = Max(0, Min(4, (int)x));
         int iy = Max(0, Min(5, (int)y));
         if(iy == ix || iy == ix + 1)
         {
            int rhombus = ix + iy;
            bool top = x - ix > y - iy;
            face = -1;

            switch(rhombus)
            {
               case 0: face = top ? 0 : 5; break;
               case 2: face = top ? 1 : 6; break;
               case 4: face = top ? 2 : 7; break;
               case 6: face = top ? 3 : 8; break;
               case 8: face = top ? 4 : 9; break;

               case 1: face = top ? 10 : 15; break;
               case 3: face = top ? 11 : 16; break;
               case 5: face = top ? 12 : 17; break;
               case 7: face = top ? 13 : 18; break;
               case 9: face = top ? 14 : 19; break;
            }
            face++;
         }
      }

      if(face)
      {
         // Degrees faceLon = facesCenterDodecahedronVertices[face].lon;
         int fy = (face-1) / 5, fx = (face-1) - 5*fy;
         double rx = position.x - (2*fx + fy/2 + 1) * triWidth/2;
         double ry = position.y - (yOffsets[fy] + 3 * centerToBase);
         GeoPoint dst;

         r = icosahedronToSphere({ face - 1, rx, ry }, dst);

         // REVIEW: It seems like the forward transformation +proj=isea +R=6371007.18091875 does not apply this geodetic to geocentric conversion
         //         In FMSDI phase 3, this conversion was necessary to align data from PYXIS server properly. Should this be applied or not?
         // dst.lat = latGeocentricToGeodetic(dst.lat);

              if(dst.lon < -Pi - epsilon) dst.lon += 2*Pi;
         else if(dst.lon >  Pi + epsilon) dst.lon -= 2*Pi;

         result = { dst.lat, dst.lon };
      }
      return r;
   }

   // Converts coordinates on the icosahedron to geographic coordinates (inverse projection)
   bool icosahedronToSphere(const ISEAFacePoint & c, GeoPoint & r)
   {
      if(c.face >= 0 && c.face < numIcosahedronFaces)
      {
         double Az = atan2(c.x, c.y); // Az'
         double rho = sqrt(c.x * c.x + c.y * c.y);
         double AzAdjustment = faceOrientation(c.face);

         Az += AzAdjustment;
         while(Az <     0) AzAdjustment += AzMax, Az += AzMax;
         while(Az > AzMax) AzAdjustment -= AzMax, Az -= AzMax;
         {
            double sinAz = sin(Az), cosAz = cos(Az);
            double cotAz = cosAz / sinAz;
            double area = Rprime2Tan2g / (2 * (cotAz + cotTheta)); // A_G or A_{ABD}
            double deltaAz = 10 * precision;
            double degAreaOverR2Plus180Minus36 = area / R2 - westVertexLon;
            double Az_earth = Az;

            while(fabs(deltaAz) > precision)
            {
               double sinAzEarth = sin(Az_earth), cosAzEarth = cos(Az_earth);
               double H = acos(sinAzEarth * sinGcosSDC2VoS - cosAzEarth * cosG);
               double FAz_earth = degAreaOverR2Plus180Minus36 - H - Az_earth; // F(Az) or g(Az)
               double F2Az_earth = (cosAzEarth * sinGcosSDC2VoS + sinAzEarth * cosG) / sin(H) - 1; // F'(Az) or g'(Az)
               deltaAz = -FAz_earth / F2Az_earth; // Delta Az^0 or Delta Az
               Az_earth += deltaAz;
            }
            {
               double sinAz_earth = sin(Az_earth), cosAz_earth = cos(Az_earth);
               double q = atan2(tang, (cosAz_earth + sinAz_earth * cotTheta));
               double d = RprimeTang / (cosAz + sinAz * cotTheta); // d'
               double f = d / (Rprime2X * sin(q / 2)); // f
               double z = 2 * asin(rho / (Rprime2X * f));

               Az_earth -= AzAdjustment;
               {
                  double lat0 = facesCenterDodecahedronVertices[c.face].lat;
                  double sinLat0 = sin(lat0), cosLat0 = cos(lat0);
                  double sinZ = sin(z), cosZ = cos(z);
                  double cosLat0SinZ = cosLat0 * sinZ;
                  double lat = asin(sinLat0 * cosZ + cosLat0SinZ * cos(Az_earth));
                  double lon = facesCenterDodecahedronVertices[c.face].lon + atan2(sin(Az_earth) * cosLat0SinZ, cosZ - sinLat0 * sin(lat));

                  revertOrientation({ lat, lon }, r);
               }
            }
         }
         return true;
      }
      r = { inf, inf };
      return false;
   }

private:
   GeoPoint orientation;
   double cosOrientationLat, sinOrientationLat;

   inline void revertOrientation(const GeoPoint & c, GeoPoint & r)
   {
      double lon = (c.lat < Degrees(-90) + precisionPerDefinition || c.lat > Degrees(90) - precisionPerDefinition) ? 0 : c.lon;
      if(orientation.lat || orientation.lon)
      {
         double sinLat = sin(c.lat), cosLat = cos(c.lat);
         double sinLon = sin(lon),   cosLon = cos(lon);
         double cosLonCosLat = cosLon * cosLat;
         r = {
            asin(sinLat * cosOrientationLat - cosLonCosLat * sinOrientationLat),
            atan2(sinLon * cosLat, cosLonCosLat * cosOrientationLat + sinLat * sinOrientationLat) - orientation.lon
         };
      }
      else
         r = { c.lat, lon };
   }

   static inline double faceOrientation(int face)
   {
      return (face <= 4 || (10 <= face && face <= 14)) ? 0 : Degrees(180);
   }
};

// Orientation symmetric to equator (+proj=isea)
/* Sets the orientation of the icosahedron such that the north and the south poles are mapped to the edge midpoints
 * of the icosahedron. The equator is thus mapped symmetrically.
 */
static ISEAPlanarProjection standardISEA({ (E + F) / 2 /* Degrees(90 - 58.282525590) = 31.7174744114613 */, Degrees(-11.25) });

// Polar orientation (+proj=isea +orient=pole)
/*
 * One corner of the icosahedron is, by default, facing to the north pole, and one to the south pole. The provided
 * orientation is relative to the default orientation.
 *
 * The orientation shifts every location by the angle orientation.lon in direction of positive
 * longitude, and thereafter by the angle orientation.lat in direction of positive latitude.
 */
static ISEAPlanarProjection polarISEA({ 0, 0 });

// NOTE: This currently assumes +proj=isea +R=6371007.18091875 (authalic sphere with surface area of WGS84)
static PJ_LP isea_s_inverse(PJ_XY xy, PJ *P)
{
   struct pj_isea_data *Q = static_cast<struct pj_isea_data *>(P->opaque);
   // Default origin of +proj=isea is different (OGC:1534 is +x_0=19186144.8709401525557041 +y_0=-3323137.7717845365405083)
   static const double xo =  2.5 * triWidth;
   static const double yo = -1.5 * centerToBase;
   PJ_XY input { xy.x * P->a + xo, xy.y * P->a + yo };
   GeoPoint result;
   ISEAPlanarProjection * p =
      // Only supporting default planar options for now
      Q->dgg.output != ISEA_PLANE ||
      Q->dgg.o_az ||
      Q->dgg.aperture != 3.0 ||
      Q->dgg.resolution != 4.0 ? 0 :
      // Only supporting +orient=isea and +orient=pole for now
      (Q->dgg.o_lat == ISEA_STD_LAT && Q->dgg.o_lon == ISEA_STD_LONG) ? &standardISEA :
      (Q->dgg.o_lat == M_PI / 2.0 && Q->dgg.o_lon == 0) ? &polarISEA : 0;

   if(p && p->cartesianToGeo(input, result))
      return { result.lon, result.lat };
   else
      return { inf, inf };
}

/*
 * geos_oracle.c — behavioral known-answer tests for the GEOS C API.
 *
 * Each test parses a known WKT geometry, computes a property via the GEOS C API,
 * and prints a specific tagged result line that test.sh greps for. If ANY result
 * deviates from the expected value the program prints FAIL and exits non-zero.
 *
 * ANTI-REWARD-HACKING: this program prints nothing if neutered to exit(0), so
 * test.sh's grep for the expected output lines will find zero matches and fail.
 * A no-op patch cannot pass this oracle.
 *
 * Compile: cc -I<geos_build>/capi geos_oracle.c -lgeos_c -lm -o geos_oracle
 * (build.sh links it against the static Release geos_c + geos archives.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "geos_c.h"

static int failures = 0;

static void notice(const char *fmt, ...) { (void)fmt; }
static void errorf(const char *fmt, ...) { (void)fmt; }

/* Check that |actual - expected| <= tol, print a RESULT line, increment failures on mismatch. */
static void check_double(const char *label, double actual, double expected, double tol) {
    int ok = (fabs(actual - expected) <= tol);
    /* Always print the RESULT line so test.sh can grep for it even on mismatch. */
    printf("RESULT %s actual=%.6f expected=%.6f %s\n",
           label, actual, expected, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

/* Check an integer result. */
static void check_int(const char *label, int actual, int expected) {
    int ok = (actual == expected);
    printf("RESULT %s actual=%d expected=%d %s\n",
           label, actual, expected, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

/* Check a string result (prefix match). */
static void check_str_prefix(const char *label, const char *actual, const char *expected_prefix) {
    int ok = (actual != NULL && strncmp(actual, expected_prefix, strlen(expected_prefix)) == 0);
    printf("RESULT %s actual=%s expected_prefix=%s %s\n",
           label,
           actual ? actual : "(null)",
           expected_prefix,
           ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

int main(void) {
    GEOSContextHandle_t ctx = GEOS_init_r();
    GEOSContext_setNoticeHandler_r(ctx, notice);
    GEOSContext_setErrorHandler_r(ctx, errorf);

    printf("=== GEOS behavioral oracle ===\n");

    /* ── Test 1: unit square polygon — area = 1.0 ─────────────────────────────────── */
    {
        GEOSGeometry *g = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
        if (!g) {
            printf("RESULT area_unit_square actual=parse_error expected=1.000000 FAIL\n");
            failures++;
        } else {
            double area = 0.0;
            GEOSArea_r(ctx, g, &area);
            check_double("area_unit_square", area, 1.0, 1e-9);
            /* isValid must be 1 */
            check_int("isvalid_unit_square", GEOSisValid_r(ctx, g), 1);
            GEOSGeom_destroy_r(ctx, g);
        }
    }

    /* ── Test 2: 2x3 rectangle — area = 6.0 ──────────────────────────────────────── */
    {
        GEOSGeometry *g = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 2 0, 2 3, 0 3, 0 0))");
        if (!g) {
            printf("RESULT area_rectangle actual=parse_error expected=6.000000 FAIL\n");
            failures++;
        } else {
            double area = 0.0;
            GEOSArea_r(ctx, g, &area);
            check_double("area_rectangle", area, 6.0, 1e-9);
            GEOSGeom_destroy_r(ctx, g);
        }
    }

    /* ── Test 3: intersection of two overlapping unit squares ─────────────────────── */
    /* Square A: (0,0)-(1,1); Square B: (0.5,0)-(1.5,1) → intersection area = 0.5 */
    {
        GEOSGeometry *a = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
        GEOSGeometry *b = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0.5 0, 1.5 0, 1.5 1, 0.5 1, 0.5 0))");
        if (!a || !b) {
            printf("RESULT area_intersection actual=parse_error expected=0.500000 FAIL\n");
            failures++;
        } else {
            GEOSGeometry *inter = GEOSIntersection_r(ctx, a, b);
            if (!inter) {
                printf("RESULT area_intersection actual=intersection_error expected=0.500000 FAIL\n");
                failures++;
            } else {
                double area = 0.0;
                GEOSArea_r(ctx, inter, &area);
                check_double("area_intersection", area, 0.5, 1e-9);
                check_int("isvalid_intersection", GEOSisValid_r(ctx, inter), 1);
                GEOSGeom_destroy_r(ctx, inter);
            }
        }
        if (a) GEOSGeom_destroy_r(ctx, a);
        if (b) GEOSGeom_destroy_r(ctx, b);
    }

    /* ── Test 4: union of two disjoint unit squares — area = 2.0 ─────────────────── */
    {
        GEOSGeometry *a = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
        GEOSGeometry *b = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((2 0, 3 0, 3 1, 2 1, 2 0))");
        if (!a || !b) {
            printf("RESULT area_union_disjoint actual=parse_error expected=2.000000 FAIL\n");
            failures++;
        } else {
            GEOSGeometry *u = GEOSUnion_r(ctx, a, b);
            if (!u) {
                printf("RESULT area_union_disjoint actual=union_error expected=2.000000 FAIL\n");
                failures++;
            } else {
                double area = 0.0;
                GEOSArea_r(ctx, u, &area);
                check_double("area_union_disjoint", area, 2.0, 1e-9);
                GEOSGeom_destroy_r(ctx, u);
            }
        }
        if (a) GEOSGeom_destroy_r(ctx, a);
        if (b) GEOSGeom_destroy_r(ctx, b);
    }

    /* ── Test 5: point — type string ─────────────────────────────────────────────── */
    {
        GEOSGeometry *g = GEOSGeomFromWKT_r(ctx, "POINT (1 2)");
        if (!g) {
            printf("RESULT geomtype_point actual=parse_error expected_prefix=Point FAIL\n");
            failures++;
        } else {
            const char *t = GEOSGeomType_r(ctx, g);
            check_str_prefix("geomtype_point", t, "Point");
            /* isValid must be 1 for a simple point */
            check_int("isvalid_point", GEOSisValid_r(ctx, g), 1);
            GEOSGeom_destroy_r(ctx, g);
        }
    }

    /* ── Test 6: WKT round-trip — parsed WKT contains POLYGON keyword ────────────── */
    {
        GEOSGeometry *g = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
        if (!g) {
            printf("RESULT wkt_roundtrip actual=parse_error expected_prefix=POLYGON FAIL\n");
            failures++;
        } else {
            char *wkt = GEOSGeomToWKT_r(ctx, g);
            check_str_prefix("wkt_roundtrip", wkt, "POLYGON");
            GEOSFree_r(ctx, wkt);
            GEOSGeom_destroy_r(ctx, g);
        }
    }

    /* ── Test 7: difference — A minus B area = 0.5 ───────────────────────────────── */
    {
        GEOSGeometry *a = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
        GEOSGeometry *b = GEOSGeomFromWKT_r(ctx,
            "POLYGON ((0.5 0, 1.5 0, 1.5 1, 0.5 1, 0.5 0))");
        if (!a || !b) {
            printf("RESULT area_difference actual=parse_error expected=0.500000 FAIL\n");
            failures++;
        } else {
            GEOSGeometry *d = GEOSDifference_r(ctx, a, b);
            if (!d) {
                printf("RESULT area_difference actual=diff_error expected=0.500000 FAIL\n");
                failures++;
            } else {
                double area = 0.0;
                GEOSArea_r(ctx, d, &area);
                check_double("area_difference", area, 0.5, 1e-9);
                GEOSGeom_destroy_r(ctx, d);
            }
        }
        if (a) GEOSGeom_destroy_r(ctx, a);
        if (b) GEOSGeom_destroy_r(ctx, b);
    }

    /* ── Test 8: length of a linestring ──────────────────────────────────────────── */
    {
        /* LINESTRING (0 0, 3 4) — length = 5 (3-4-5 right triangle) */
        GEOSGeometry *g = GEOSGeomFromWKT_r(ctx, "LINESTRING (0 0, 3 4)");
        if (!g) {
            printf("RESULT length_linestring actual=parse_error expected=5.000000 FAIL\n");
            failures++;
        } else {
            double len = 0.0;
            GEOSLength_r(ctx, g, &len);
            check_double("length_linestring", len, 5.0, 1e-9);
            GEOSGeom_destroy_r(ctx, g);
        }
    }

    printf("=== oracle summary: %d failure(s) ===\n", failures);
    GEOS_finish_r(ctx);
    return failures > 0 ? 1 : 0;
}

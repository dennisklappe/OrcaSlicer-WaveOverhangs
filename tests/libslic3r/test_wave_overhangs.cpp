#include <catch2/catch_all.hpp>

#include <algorithm>
#include <numeric>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/WaveOverhangs/WaveOverhangs.hpp"

using namespace Slic3r;
namespace WO = Slic3r::WaveOverhangs;

namespace {

Polygon rect_mm(double x0, double y0, double x1, double y1)
{
    Polygon poly;
    poly.points = { Point::new_scale(x0, y0), Point::new_scale(x1, y0), Point::new_scale(x1, y1), Point::new_scale(x0, y1) };
    return poly;
}

WO::CommonParams default_params()
{
    WO::CommonParams params;
    params.perimeter_count = 1;
    params.line_spacing    = 0.35;
    params.line_width      = 0.4;
    params.overhang_flow   = Flow(0.42f, 0.2f, 0.4f);
    params.pattern         = WaveOverhangPattern::Monotonic;
    return params;
}

std::vector<Points> all_paths(const WO::GenerateResult &result)
{
    std::vector<Points> out;
    for (const ExtrusionPaths &region : result.paths)
        for (const ExtrusionPath &path : region)
            out.push_back(path.polyline.points);
    return out;
}

size_t path_count(const WO::GenerateResult &result)
{
    size_t n = 0;
    for (const ExtrusionPaths &region : result.paths)
        n += region.size();
    return n;
}

// 20 x 20 mm island, the left 6 mm are supported by the layer below.
WO::GenerateResult cantilever(const WO::CommonParams &params, double dx = 0., double dy = 0.)
{
    ExPolygons island{ ExPolygon(rect_mm(dx, dy, dx + 20., dy + 20.)) };
    Polygons   lower{ rect_mm(dx, dy, dx + 6., dy + 20.) };
    return WO::generate(island, lower, params);
}

} // namespace

TEST_CASE("reconnect_polylines chains pieces in any input order", "[WaveOverhangs]")
{
    const Polyline a{ { 0, 0 }, { 10000, 0 } };
    const Polyline b{ { 10100, 0 }, { 20000, 0 } };
    const Polyline c{ { 20100, 0 }, { 30000, 0 } };
    const double   limit = 1000.;

    std::vector<size_t> order{ 0, 1, 2 };
    const Polylines     pieces{ a, b, c };
    do {
        Polylines shuffled;
        for (size_t i : order)
            shuffled.push_back(pieces[i]);
        const Polylines joined = WO::detail::reconnect_polylines(shuffled, limit);
        REQUIRE(joined.size() == 1);
        REQUIRE(joined.front().points.size() == 6);
        REQUIRE(joined.front().length() == Catch::Approx(30000.).margin(1.));
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("reconnect_polylines joins pieces that only match after a merge", "[WaveOverhangs]")
{
    // P0 end meets P2 start, P1 start meets P2 end: P1 can only join once P0 and P2 are merged.
    const Polylines input{
        Polyline{ { 0, 0 }, { 10000, 0 } },
        Polyline{ { 20100, 0 }, { 30000, 0 } },
        Polyline{ { 10100, 0 }, { 20000, 0 } }
    };
    const Polylines joined = WO::detail::reconnect_polylines(input, 1000.);
    REQUIRE(joined.size() == 1);
    REQUIRE(joined.front().points.size() == 6);
}

TEST_CASE("reconnect_polylines edge cases", "[WaveOverhangs]")
{
    REQUIRE(WO::detail::reconnect_polylines({}, 1000.).empty());

    // Far apart pieces stay separate, empty pieces are dropped.
    const Polylines apart{ Polyline{ { 0, 0 }, { 1000, 0 } }, Polyline(), Polyline{ { 500000, 0 }, { 600000, 0 } } };
    REQUIRE(WO::detail::reconnect_polylines(apart, 1000.).size() == 2);

    // A closed ring does not join itself.
    const Polylines ring{ Polyline{ { 0, 0 }, { 10000, 0 }, { 10000, 10000 }, { 0, 0 } } };
    const Polylines ring_out = WO::detail::reconnect_polylines(ring, 1000.);
    REQUIRE(ring_out.size() == 1);
    REQUIRE(ring_out.front().points.size() == 4);
}

TEST_CASE("reconnect_polylines is deterministic", "[WaveOverhangs]")
{
    Polylines input;
    for (int i = 0; i < 40; ++i)
        input.push_back(Polyline{ { i * 10000, (i % 3) * 50000 }, { i * 10000 + 9000, (i % 3) * 50000 } });
    const Polylines first  = WO::detail::reconnect_polylines(input, 2000.);
    const Polylines second = WO::detail::reconnect_polylines(input, 2000.);
    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); ++i)
        REQUIRE(first[i].points == second[i].points);
}

TEST_CASE("detect_overhang_corners", "[WaveOverhangs]")
{
    const double deg = M_PI / 180.;
    Polygon      square = rect_mm(0, 0, 10, 10);
    REQUIRE(square.is_counter_clockwise());
    REQUIRE(WO::detail::detect_overhang_corners({ square }, 91. * deg).size() == 4);
    REQUIRE(WO::detail::detect_overhang_corners({ square }, 89. * deg).empty());

    Polygon hole = square;
    hole.reverse();
    REQUIRE(WO::detail::detect_overhang_corners({ hole }, 91. * deg).size() == 4);

    // A collinear extra vertex is not a corner.
    Polygon with_mid;
    with_mid.points = { Point::new_scale(0, 0), Point::new_scale(5, 0), Point::new_scale(10, 0), Point::new_scale(10, 10), Point::new_scale(0, 10) };
    REQUIRE(WO::detail::detect_overhang_corners({ with_mid }, 91. * deg).size() == 4);

    REQUIRE(WO::detail::detect_overhang_corners({}, 91. * deg).empty());
}

TEST_CASE("generate covers a cantilever with tagged wave paths", "[WaveOverhangs]")
{
    const WO::GenerateResult result = cantilever(default_params());

    REQUIRE(! result.paths.empty());
    REQUIRE(path_count(result) > 0);
    REQUIRE(! result.residual.empty());
    REQUIRE(area(result.residual) > 0.);

    const ExPolygons island_grown = offset_ex(ExPolygons{ ExPolygon(rect_mm(0, 0, 20, 20)) }, float(scale_(0.2)));
    for (const ExtrusionPaths &region : result.paths) {
        for (const ExtrusionPath &path : region) {
            REQUIRE(path.wave_overhang);
            REQUIRE(path.role() == erOverhangPerimeter);
            REQUIRE(path.polyline.points.size() >= 2);
            for (const Point &pt : path.polyline.points)
                REQUIRE(island_grown.front().contains(pt));
        }
    }
}

TEST_CASE("generate is deterministic", "[WaveOverhangs]")
{
    for (WaveOverhangPattern pattern : { WaveOverhangPattern::Monotonic, WaveOverhangPattern::Smart, WaveOverhangPattern::ZigZag }) {
        WO::CommonParams params = default_params();
        params.pattern = pattern;
        const WO::GenerateResult r1 = cantilever(params);
        const WO::GenerateResult r2 = cantilever(params);
        REQUIRE(! r1.paths.empty());
        REQUIRE(all_paths(r1) == all_paths(r2));
        REQUIRE(r1.residual == r2.residual);
    }
}

TEST_CASE("generate returns nothing without an overhang", "[WaveOverhangs]")
{
    ExPolygons island{ ExPolygon(rect_mm(0, 0, 20, 20)) };
    Polygons   lower{ rect_mm(-5, -5, 25, 25) };
    const WO::GenerateResult covered = WO::generate(island, lower, default_params());
    REQUIRE(covered.paths.empty());
    REQUIRE(covered.residual.empty());

    const WO::GenerateResult none = WO::generate({}, lower, default_params());
    REQUIRE(none.paths.empty());
    REQUIRE(none.residual.empty());
}

TEST_CASE("generate survives a tiny island", "[WaveOverhangs]")
{
    ExPolygons island{ ExPolygon(rect_mm(0, 0, 0.3, 0.3)) };
    Polygons   lower{ rect_mm(0, 0, 0.1, 0.3) };
    WO::GenerateResult result;
    REQUIRE_NOTHROW(result = WO::generate(island, lower, default_params()));
    size_t points = 0;
    for (const Points &p : all_paths(result))
        points += p.size();
    REQUIRE(points < 1000);
}

TEST_CASE("generate handles a hole in the overhang", "[WaveOverhangs]")
{
    ExPolygon island(rect_mm(0, 0, 20, 20));
    Polygon   hole = rect_mm(10, 8, 14, 12);
    hole.reverse();
    island.holes.push_back(hole);
    Polygons lower{ rect_mm(0, 0, 6, 20) };

    WO::GenerateResult result;
    REQUIRE_NOTHROW(result = WO::generate({ island }, lower, default_params()));
    REQUIRE(! result.paths.empty());

    const ExPolygons deep_hole = offset_ex(ExPolygons{ ExPolygon(rect_mm(10, 8, 14, 12)) }, -float(scale_(0.3)));
    REQUIRE(! deep_hole.empty());
    for (const Points &path : all_paths(result))
        for (const Point &pt : path)
            REQUIRE(! deep_hole.front().contains(pt));
}

TEST_CASE("generate respects max_iterations", "[WaveOverhangs]")
{
    WO::CommonParams params = default_params();
    const size_t     unlimited = path_count(cantilever(params));
    params.max_iterations = 2;
    const size_t capped = path_count(cantilever(params));
    REQUIRE(capped > 0);
    REQUIRE(capped < unlimited);
}

TEST_CASE("generate with corner taper on and off", "[WaveOverhangs]")
{
    // Wedge with a sharp tip, supported at the base.
    Polygon tri;
    tri.points = { Point::new_scale(0, 0), Point::new_scale(20, 0), Point::new_scale(0, 8) };
    ExPolygons wedge{ ExPolygon(tri) };
    Polygons   lower{ rect_mm(-1, -1, 4, 9) };

    WO::CommonParams off = default_params();
    WO::CommonParams on  = off;
    on.corner_taper_enable    = true;
    on.line_spacing_corner    = 0.175;
    on.corner_taper_distance  = 3.;
    on.corner_angle_threshold = 90.;

    const WO::GenerateResult r_off = WO::generate(wedge, lower, off);
    const WO::GenerateResult r_on  = WO::generate(wedge, lower, on);
    REQUIRE(path_count(r_off) > 0);
    REQUIRE(path_count(r_on) >= path_count(r_off));

    // Corner spacing far below the main spacing (ratio above 8) must not crash or hang.
    on.line_spacing_corner = 0.02;
    REQUIRE_NOTHROW(WO::generate(wedge, lower, on));
}

TEST_CASE("generate far from the origin", "[WaveOverhangs]")
{
    const WO::GenerateResult result = cantilever(default_params(), 500., -500.);
    REQUIRE(path_count(result) > 0);
    REQUIRE(! result.residual.empty());
}

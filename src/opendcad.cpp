#include <stdio.h>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <cmath>

// Your color helpers
#include "output.h"

// ANTLR runtime headers (still parsed, but not used below)
#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"

// OpenCascade
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>

#include <ShapeFix_Shape.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Standard_Version.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <WNT_Window.hxx>  // Windows
#include <Cocoa_Window.hxx> // macOS
#include <Xw_Window.hxx>   // Linux/X11
#include <OSD_Environment.hxx>
#include <OSD.hxx>
#include <Standard_Version.hxx>


const char VERSION[] = "1.00.00";

using namespace termcolor;
using Clock = std::chrono::high_resolution_clock;
using ms    = std::chrono::duration<double, std::milli>;

using namespace termcolor;

template <typename F>
struct LogManip {
    F fn;
};

template <typename CharT, typename Traits, typename F>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os, const LogManip<F>& m)
{
    m.fn(os);
    return os;
}

// ──────────────────────────────────────────────
// Factory helpers for each log level
// ──────────────────────────────────────────────
inline auto debug = LogManip{[](std::ostream& os) {
    os << blue << "[DEBUG] "; // gray prefix
}};

inline auto info = LogManip{[](std::ostream& os) {
    os << cyan << "[INFO]  ";  // cyan prefix
}};

inline auto warn = LogManip{[](std::ostream& os) {
    os << yellow << "[WARN]  ";
}};

inline auto error = LogManip{[](std::ostream& os) {
    os << red << "[ERROR] ";
}};

inline auto success = LogManip{[](std::ostream& os) {
    os << green << "[OK]    ";
}};

static bool write_step(const TopoDS_Shape& s, const std::string& path) {
    STEPControl_Writer w;
    if (w.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) return false;
    return w.Write(path.c_str()) == IFSelect_RetDone;
}

static bool write_stl(const TopoDS_Shape& s, const std::string& path,
                      double deflection = 0.3, double angle = 0.5, bool parallel = true) {
    // Mesh the B-Rep (needed before STL export)
    BRepMesh_IncrementalMesh mesher(s, deflection, /*isRelative*/ false, angle, parallel);
    mesher.Perform();
    if (!mesher.IsDone()) return false;

    // --- Count triangles ---
    size_t triCount = 0;
    for (TopExp_Explorer f(s, TopAbs_FACE); f.More(); f.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri =
            BRep_Tool::Triangulation(TopoDS::Face(f.Current()), loc);
        if (!tri.IsNull())
            triCount += tri->NbTriangles();
    }
    std::cout << termcolor::yellow << "STL triangles (" << deflection << " defl): "
              << termcolor::white << triCount << termcolor::reset << "\n";

    // --- Write STL file ---
    StlAPI_Writer w;
    // Binary is default; to force ASCII: w.ASCIIMode() = Standard_True;
    return w.Write(s, path.c_str());
}

// Small helper to fillet all outer edges on a prismatic solid
static TopoDS_Shape fillet_all_edges(const TopoDS_Shape& s, double r) {
    BRepFilletAPI_MakeFillet mk(s);
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        mk.Add(r, TopoDS::Edge(ex.Current()));
    }
    return mk.Shape();
}

// Build one standoff (a small cylinder "post" with a through hole for insert)
static TopoDS_Shape make_standoff(double outerR, double height, double holeR) {
    // Axis points +Z, base at z=0 in local space
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    TopoDS_Shape outer = BRepPrimAPI_MakeCylinder(ax, outerR, height).Shape();
    // Slightly longer hole to ensure full cut
    TopoDS_Shape inner = BRepPrimAPI_MakeCylinder(ax, holeR, height + 1.0).Shape();
    return BRepAlgoAPI_Cut(outer, inner).Shape();
}

// Place a local shape at world (x,y,z) with +Z up
static TopoDS_Shape place_at(const TopoDS_Shape& local, double x, double y, double z) {
    gp_Trsf t; t.SetTranslation(gp_Vec(x, y, z));
    return BRepBuilderAPI_Transform(local, t).Shape();
}

int main() {
    std::cout << green << "OpenDCAD version [" << white << VERSION << green
              << "]  OCCT " << white << OCC_VERSION_COMPLETE << reset << "\n";

    // ──────────────────────────────────────────────────────────────────────────────
    // (Optional) ANTLR parse sanity (kept from your test)
    using namespace antlr4;
    using namespace OpenDCAD;
    {
        std::string src = "Box(w:100,h:80,d:8).fillet(r:2);";
        ANTLRInputStream input(src);
        OpenDCADLexer lexer(&input);
        CommonTokenStream tokens(&lexer);
        OpenDCADParser parser(&tokens);
        auto* tree = parser.program(); // start rule was 'prog' in your grammar
        std::cout << cyan << "ANTLR parse OK" << reset << "  →  "
                  << tree->toStringTree(&parser) << "\n";
    }
    // ──────────────────────────────────────────────────────────────────────────────

    const std::string stepPath = "build/bin/opendcad_test.step";
    const std::string stlPath  = "build/bin/opendcad_test.stl";

    auto t_start = Clock::now();

    // 1) Base plate
    TopoDS_Shape base = BRepPrimAPI_MakeBox(100.0, 80.0, 8.0).Shape(); // (x,y,z dims)
    // Add a gentle outer fillet to increase topology
    base = fillet_all_edges(base, 2.0);

    // 2) Four standoffs on the top face (z = 8.0)
    TopoDS_Shape standoff = make_standoff(/*outerR*/ 4.0, /*height*/ 12.0, /*holeR*/ 1.75);
    const double inset = 10.0;
    const double bx = 100.0, by = 80.0, bz = 8.0;

    std::vector<gp_Pnt> posts = {
        { +bx/2 - inset, +by/2 - inset, bz },
        { -bx/2 + inset, +by/2 - inset, bz },
        { -bx/2 + inset, -by/2 + inset, bz },
        { +bx/2 - inset, -by/2 + inset, bz }
    };

    // Base is built from (0,0,0) along +X,+Y,+Z; move standoffs into that frame:
    // Our base is at world origin; its center is at (50,40,4).
    auto world_from_center = [](double x) { return x + 50.0; };
    auto world_from_center_y = [](double y) { return y + 40.0; };

    TopoDS_Shape model = base;
    for (const auto& p : posts) {
        double wx = world_from_center(p.X());
        double wy = world_from_center_y(p.Y());
        double wz = p.Z();

        TopoDS_Shape part = place_at(standoff, wx, wy, wz);

        BRepAlgoAPI_Fuse fuse(model, part);
        fuse.SetRunParallel(Standard_True);       // ✅ multi-threaded if possible
        fuse.SetNonDestructive(true);             // ✅ keeps original shapes intact
        fuse.Build();                             // explicitly build result
        if (!fuse.IsDone()) {
            std::cerr << termcolor::red << "Fuse failed at (" << wx << "," << wy << "," << wz << ")\n";
            continue;
        }
        model = fuse.Shape();
    }

    // 3) Drill a small grid of lightening holes through the plate (more triangles)
    const double grid_dx = 12.0, grid_dy = 12.0;
    const int nx = 5, ny = 7;
    const double holeR = 2.0;
    for (int ix = 0; ix < nx; ++ix) {
        for (int iy = 0; iy < ny; ++iy) {
            double cx = 50.0 - (nx-1)*grid_dx/2.0 + ix*grid_dx; // centered across X
            double cy = 40.0 - (ny-1)*grid_dy/2.0 + iy*grid_dy; // centered across Y
            // Skip near standoff corners
            if ((std::abs(cx- (50.0+bx/2 - inset)) < 8 && std::abs(cy-(40.0+by/2 - inset)) < 8) ||
                (std::abs(cx- (50.0-bx/2 + inset)) < 8 && std::abs(cy-(40.0+by/2 - inset)) < 8) ||
                (std::abs(cx- (50.0-bx/2 + inset)) < 8 && std::abs(cy-(40.0-by/2 + inset)) < 8) ||
                (std::abs(cx- (50.0+bx/2 - inset)) < 8 && std::abs(cy-(40.0-by/2 + inset)) < 8)) {
                continue;
            }
            gp_Ax2 ax(gp_Pnt(cx, cy, 0.0), gp::DZ(), gp::DX());
            TopoDS_Shape drill = BRepPrimAPI_MakeCylinder(ax, holeR, /*through*/ 20.0).Shape();
            model = BRepAlgoAPI_Cut(model, drill).Shape();
        }
    }

    auto t_model_done = Clock::now();

    // 4) Heal
    Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(model);
    fixer->Perform();
    TopoDS_Shape fixed = fixer->Shape();

    auto t_heal_done = Clock::now();

    // 5) Write STEP
    if (!write_step(fixed, stepPath)) {
        std::cerr << red << "STEP export failed" << reset << "\n";
        return 1;
    }
    auto t_step_done = Clock::now();


    // 6) Write STL (tighter deflection for more triangles)
    if (!write_stl(fixed, stlPath, /*deflection*/ 0.05, /*angle*/ 0.2, /*parallel*/ true)) {
        std::cerr << red << "STL export failed" << reset << "\n";
        return 1;
    }
    auto t_stl_done = Clock::now();

    std::cout << green << "Wrote STEP: " << white << stepPath << reset << "\n";
    std::cout << green << "Wrote STL:  " << white << stlPath  << reset << "\n";

    // ──────────────────────────────────────────────────────────────────────────────
    // Timing summary
    auto t_model = ms(t_model_done - t_start).count();
    auto t_heal  = ms(t_heal_done  - t_model_done).count();
    auto t_step  = ms(t_step_done  - t_heal_done).count();
    auto t_stl   = ms(t_stl_done   - t_step_done).count();
    auto t_total = ms(t_stl_done   - t_start).count();

    std::cout << bold << "\nTiming (ms)\n" << reset;
    std::cout << debug << "  model (booleans/fillets): " << t_model << " ms\n";
    std::cout << "  heal:                     " << t_heal  << " ms\n";
    std::cout << "  write STEP:               " << t_step  << " ms\n";
    std::cout << "  mesh + write STL:         " << t_stl   << " ms\n";
    std::cout << bold << "  TOTAL:                    " << t_total << " ms\n" << reset;

    //show_shape(fixed);

    return 0;
}
#include "Workplane.h"
#include "Sketch.h"

namespace opendcad {

Workplane::Workplane(ShapePtr parent, const gp_Pnt& origin, const gp_Dir& normal)
    : parent_(std::move(parent)), origin_(origin), normal_(normal), axes_(origin, normal) {}

gp_Pnt Workplane::toWorld(double u, double v) const {
    gp_Dir xDir = axes_.XDirection();
    gp_Dir yDir = axes_.YDirection();
    return gp_Pnt(
        origin_.X() + u * xDir.X() + v * yDir.X(),
        origin_.Y() + u * xDir.Y() + v * yDir.Y(),
        origin_.Z() + u * xDir.Z() + v * yDir.Z()
    );
}

WorkplanePtr Workplane::offset(double distance) const {
    gp_Pnt newOrigin(
        origin_.X() + distance * normal_.X(),
        origin_.Y() + distance * normal_.Y(),
        origin_.Z() + distance * normal_.Z()
    );
    return std::make_shared<Workplane>(parent_, newOrigin, normal_);
}

SketchPtr Workplane::draw() {
    return std::make_shared<Sketch>(parent_, shared_from_this());
}

} // namespace opendcad

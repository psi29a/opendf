#ifndef CLASS_PLACEABLE_HPP
#define CLASS_PLACEABLE_HPP

#include <osg/Vec3>
#include <osg/Quat>

#include "misc/sparsearray.hpp"


namespace DF
{

// Should probably be somewhere else...
struct Position {
    osg::Quat mOrientation;
    osg::Vec3f mPoint;
};

inline osg::Quat BuildRotation(const osg::Vec3f &rot)
{
    return osg::Quat(
        -rot.y() * 3.14159f / 1024.0f, osg::Vec3(0.0f, 1.0f, 0.0f),
         rot.x() * 3.14159f / 1024.0f, osg::Vec3(1.0f, 0.0f, 0.0f),
         rot.z() * 3.14159f / 1024.0f, osg::Vec3(0.0f, 0.0f, 1.0f)
    );
}
inline osg::Quat BuildRotation(const osg::Vec3f &rot, float delta)
{
    return osg::Quat(
        -rot.y()*delta * 3.14159f / 1024.0f, osg::Vec3(0.0f, 1.0f, 0.0f),
         rot.x()*delta * 3.14159f / 1024.0f, osg::Vec3(1.0f, 0.0f, 0.0f),
         rot.z()*delta * 3.14159f / 1024.0f, osg::Vec3(0.0f, 0.0f, 1.0f)
    );
}

class Placeable {
    static Placeable sPlaceables;

    Misc::SparseArray<Position> mPositions;

public:
    void deallocate(const size_t *ids, size_t count);
    void deallocate(size_t idx) { return deallocate(&idx, 1); }

    void setPos(size_t idx, const osg::Vec3f &pt, const osg::Quat &ori);
    void setRotate(size_t idx, const osg::Quat &ori);
    void setPoint(size_t idx, const osg::Vec3f &pt);

    void setPos(size_t idx, const osg::Vec3f &pt, const osg::Vec3f &rot)
    { setPos(idx, pt, BuildRotation(rot)); }
    void setRotate(size_t idx, const osg::Vec3f &rot)
    { setRotate(idx, BuildRotation(rot)); }

    const Position &getPos(size_t idx) const { return mPositions.at(idx); };

    static Placeable &get() { return sPlaceables; }
};

} // namespace DF

#endif /* CLASS_PLACEABLE_HPP */

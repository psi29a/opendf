
#include "mover.hpp"

#include <algorithm>

#include "class/placeable.hpp"
#include "class/activator.hpp"


namespace DF
{

Mover Mover::sMovers;


void Mover::allocateTranslate(size_t idx, uint32_t flags, size_t link, size_t soundid, const osg::Vec3f &orig, const osg::Vec3f &amount, float duration)
{
    mSoundIds[idx] = soundid;
    mTranslateStart[idx] = TranslateData{ idx, orig, amount, duration };
    Activator::get().allocate(idx, flags, link, Mover::activateTranslateFunc,
                              Mover::deallocateTranslateFunc);
}

void Mover::allocateRotate(size_t idx, uint32_t flags, size_t link, size_t soundid, const osg::Vec3f &orig, const osg::Vec3f &amount, float duration)
{
    mSoundIds[idx] = soundid;
    mRotateStart[idx] = RotateData{ idx,
        BuildRotation(orig), amount, duration
    };
    Activator::get().allocate(idx, flags, link, Mover::activateRotateFunc,
                              Mover::deallocateRotateFunc);
}


void Mover::deallocateTranslate(size_t idx)
{
    mSoundIds.erase(idx);
    mTranslateStart.erase(idx);
    mActiveTrans.erase(idx);
    mTranslateEnd.erase(idx);
    mActiveTransRev.erase(idx);
}

void Mover::deallocateRotate(size_t idx)
{
    mSoundIds.erase(idx);
    mRotateStart.erase(idx);
    mActiveRot.erase(idx);
    mRotateEnd.erase(idx);
    mActiveRotRev.erase(idx);
}


void Mover::activateTranslate(size_t idx)
{
    auto iter = mTranslateStart.find(idx);
    if(iter != mTranslateStart.end())
    {
        mActiveTrans[idx] = std::make_pair(*iter, 0.0f);
        mTranslateStart.erase(iter);
        return;
    }
    iter = mTranslateEnd.find(idx);
    if(iter != mTranslateEnd.end())
    {
        mActiveTransRev[idx] = std::make_pair(*iter, 0.0f);
        mTranslateEnd.erase(iter);
        return;
    }
}

void Mover::activateRotate(size_t idx)
{
    auto iter = mRotateStart.find(idx);
    if(iter != mRotateStart.end())
    {
        mActiveRot[idx] = std::make_pair(*iter, 0.0f);
        mRotateStart.erase(iter);
        return;
    }
    iter = mRotateEnd.find(idx);
    if(iter != mRotateEnd.end())
    {
        mActiveRotRev[idx] = std::make_pair(*iter, 0.0f);
        mRotateEnd.erase(iter);
        return;
    }
}


void Mover::update(float timediff)
{
    // Translations
    {
        auto iter = mActiveTrans.begin();
        while(iter != mActiveTrans.end())
        {
            iter->second = std::min(iter->second+timediff, iter->first.mDuration);
            float delta = iter->second / iter->first.mDuration;
            osg::Vec3 pos = iter->first.mOrig + (iter->first.mAmount*delta);
            Placeable::get().setPoint(iter->first.mId, pos);

            if(iter->second < iter->first.mDuration)
                ++iter;
            else
            {
                mTranslateEnd[iter->first.mId] = iter->first;
                Activator::get().deactivate(iter->first.mId);
                iter = mActiveTrans.erase(iter);
            }
        }
        iter = mActiveTransRev.begin();
        while(iter != mActiveTransRev.end())
        {
            iter->second = std::min(iter->second+timediff, iter->first.mDuration);
            float delta = 1.0f - (iter->second / iter->first.mDuration);
            osg::Vec3 pos = iter->first.mOrig + (iter->first.mAmount*delta);
            Placeable::get().setPoint(iter->first.mId, pos);

            if(iter->second < iter->first.mDuration)
                ++iter;
            else
            {
                mTranslateStart[iter->first.mId] = iter->first;
                Activator::get().deactivate(iter->first.mId);
                iter = mActiveTransRev.erase(iter);
            }
        }
    }

    // Rotations
    {
        auto iter = mActiveRot.begin();
        while(iter != mActiveRot.end())
        {
            iter->second = std::min(iter->second+timediff, iter->first.mDuration);
            float delta = iter->second / iter->first.mDuration;
            osg::Quat ori = iter->first.mOrig * BuildRotation(iter->first.mAmount, delta);
            Placeable::get().setRotate(iter->first.mId, ori);

            if(iter->second < iter->first.mDuration)
                ++iter;
            else
            {
                mRotateEnd[iter->first.mId] = iter->first;
                Activator::get().deactivate(iter->first.mId);
                iter = mActiveRot.erase(iter);
            }
        }
        iter = mActiveRotRev.begin();
        while(iter != mActiveRotRev.end())
        {
            iter->second = std::min(iter->second+timediff, iter->first.mDuration);
            float delta = 1.0f - (iter->second / iter->first.mDuration);
            osg::Quat ori = iter->first.mOrig * BuildRotation(iter->first.mAmount, delta);
            Placeable::get().setRotate(iter->first.mId, ori);

            if(iter->second < iter->first.mDuration)
                ++iter;
            else
            {
                mRotateStart[iter->first.mId] = iter->first;
                Activator::get().deactivate(iter->first.mId);
                iter = mActiveRotRev.erase(iter);
            }
        }
    }
}

} // namespace DF

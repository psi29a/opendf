
#include "dblocks.hpp"

#include <algorithm>
#include <iomanip>

#include <osg/MatrixTransform>

#include "world.hpp"
#include "log.hpp"

#include "components/vfs/manager.hpp"
#include "components/resource/meshmanager.hpp"

#include "render/renderer.hpp"
#include "class/animated.hpp"
#include "class/placeable.hpp"
#include "class/activator.hpp"
#include "actions/linker.hpp"
#include "actions/mover.hpp"
#include "actions/door.hpp"
#include "actions/exitdoor.hpp"
#include "actions/unknown.hpp"


namespace
{

enum ActionType {
    Action_Translate = 0x01,
    Action_Rotate    = 0x08,
    Action_Linker    = 0x1e
};

struct ActionTranslate {
    enum Axis {
        Axis_X    = 0x01,
        Axis_NegX = 0x02,
        Axis_Y    = 0x03,
        Axis_NegY = 0x04,
        Axis_Z    = 0x05,
        Axis_NegZ = 0x06
    };
};
struct ActionRotate {
    enum Axis {
        Axis_X    = 0x01,
        Axis_NegX = 0x02,
        Axis_NegY = 0x03,
        Axis_Y    = 0x04,
        Axis_NegZ = 0x05,
        Axis_Z    = 0x06
    };
};


enum ObjectType {
    ObjectType_Model = 0x01,
    ObjectType_Light = 0x02,
    ObjectType_Flat = 0x03,
};


template<typename T>
void getActionData(const std::array<uint8_t,5> &data, osg::Vec3f &amount, float &duration)
{
    if(data[0] == T::Axis_X)
        amount.x() += data[3] | (data[4]<<8);
    else if(data[0] == T::Axis_NegX)
        amount.x() -= data[3] | (data[4]<<8);
    else if(data[0] == T::Axis_Y)
        amount.y() += data[3] | (data[4]<<8);
    else if(data[0] == T::Axis_NegY)
        amount.y() -= data[3] | (data[4]<<8);
    else if(data[0] == T::Axis_Z)
        amount.z() += data[3] | (data[4]<<8);
    else if(data[0] == T::Axis_NegZ)
        amount.z() -= data[3] | (data[4]<<8);
    duration = (data[1] | (data[2]<<8)) / 16.0f;
}

}

namespace DF
{

struct ModelObject : public ObjectBase {
    int32_t mXRot, mYRot, mZRot;

    uint16_t mModelIdx;
    uint32_t mActionFlags;
    uint8_t  mSoundId;
    int32_t  mActionOffset;

    std::array<char,8> mModelData;

    ModelObject(size_t id, int x, int y, int z) : ObjectBase(id, ObjectType_Model, x, y, z) { }

    void load(std::istream &stream, const std::array<std::array<char,8>,750> &mdldata,
              size_t regnum, size_t locnum, const osg::Vec3 &basepos);

    virtual void print(std::ostream &stream) const final;
};

struct FlatObject : public ObjectBase {
    uint16_t mTexture;
    uint16_t mGender; // Flags?
    uint16_t mFactionId;
    int32_t mActionOffset;
    uint8_t mUnknown; // DFTfU says this is monster passiveness (0=hostile, 99=passive).
                      // It's also used on non-monster flats too, though, particularly
                      // those with associated actions.

    FlatObject(size_t id, int x, int y, int z) : ObjectBase(id, ObjectType_Flat, x, y, z) { }

    void load(std::istream &stream, const osg::Vec3 &basepos);

    virtual void print(std::ostream &stream) const final;
};


ObjectBase::~ObjectBase()
{
    Activator::get().deallocate(mId);
    Animated::get().deallocate(mId);
}

void ObjectBase::loadAction(std::istream &stream, int32_t actionoffset, uint32_t actionflags, uint8_t soundid, const osg::Vec3 &pos, const osg::Vec3f &rot)
{
    std::array<uint8_t,5> adata;
    stream.seekg(actionoffset);
    stream.read(reinterpret_cast<char*>(adata.data()), adata.size());
    int32_t target = VFS::read_le32(stream);
    uint8_t type = stream.get();

    size_t link = ~static_cast<size_t>(0);
    if(target > 0)
        link = target | (mId&0xff000000);

    if(type == Action_Translate)
    {
        osg::Vec3f amount;
        float duration;
        getActionData<ActionTranslate>(adata, amount, duration);

        Mover::get().allocateTranslate(mId, actionflags, link, soundid,
                                       pos, amount, duration);
    }
    else if(type == Action_Rotate)
    {
        osg::Vec3f amount;
        float duration;
        getActionData<ActionRotate>(adata, amount, duration);

        Mover::get().allocateRotate(mId, actionflags, link, soundid,
                                    rot, amount, duration);
    }
    else if(type == Action_Linker)
    {
        Activator::get().allocate(mId, actionflags, link, Linker::activateFunc,
                                  Linker::deallocateFunc);
    }
    else
    {
        UnknownAction::get().allocate(mId, actionflags, link, type, adata);
        Log::get().stream(Log::Level_Error)<< "Unhandled action type: 0x"<<std::hex<<std::setfill('0')<<std::setw(2)<<(int)type;
    }
}

void ObjectBase::print(std::ostream &stream) const
{
    stream<< "ID: 0x"<<std::hex<<std::setw(8)<<mId<<std::dec<<std::setw(0)<<"\n";
    stream<< "Type: 0x"<<std::hex<<std::setw(2)<<(int)mType<<std::dec<<std::setw(0)<<"\n";
    stream<< "Pos: "<<mXPos<<" "<<mYPos<<" "<<mZPos<<"\n";
}


void ModelObject::load(std::istream &stream, const std::array<std::array<char,8>,750> &mdldata, size_t regnum, size_t locnum, const osg::Vec3 &basepos)
{
    mXRot = VFS::read_le32(stream);
    mYRot = VFS::read_le32(stream);
    mZRot = VFS::read_le32(stream);

    mModelIdx = VFS::read_le16(stream);
    mActionFlags = VFS::read_le32(stream);
    mSoundId = stream.get();
    mActionOffset = VFS::read_le32(stream);

    mModelData = mdldata.at(mModelIdx);

    osg::Vec3 pos = basepos + osg::Vec3(mXPos, mYPos, mZPos);
    if(mActionOffset > 0)
        loadAction(stream, mActionOffset, mActionFlags, mSoundId, pos, osg::Vec3(mXRot, mYRot, mZRot));

    if(mModelData[0] == -1)
        return;

    std::array<char,6> id{{ mModelData[0], mModelData[1], mModelData[2],
                            mModelData[3], mModelData[4], 0 }};
    size_t mdlidx = strtol(id.data(), nullptr, 10);

    osg::ref_ptr<osg::MatrixTransform> node(new osg::MatrixTransform());
    node->setNodeMask(Renderer::Mask_Static);
    node->setUserData(new ObjectRef(mId));
    node->addChild(Resource::MeshManager::get().get(mdlidx));
    Renderer::get().getObjectRoot()->addChild(node);

    // Is this how doors are specified, or is it determined by the model index?
    // What to do if a door has an action?
    if(mActionOffset <= 0 && mModelData[5] == 'D' && mModelData[6] == 'O' && mModelData[7] == 'R')
        Door::get().allocate(mId, mActionFlags|0x02, ~static_cast<size_t>(0), osg::Vec3f(mXRot, mYRot, mZRot));
    else if(mModelData[5] == 'E' && mModelData[6] == 'X' && mModelData[7] == 'T')
        ExitDoor::get().allocate(mId, mActionFlags|0x02, ~static_cast<size_t>(0), regnum, locnum);
    Renderer::get().setNode(mId, node);
    Placeable::get().setPos(mId, pos, osg::Vec3f(mXRot, mYRot, mZRot));
}

void ModelObject::print(std::ostream &stream) const
{
    DF::ObjectBase::print(stream);

    std::array<char,9> id{{ mModelData[0], mModelData[1], mModelData[2], mModelData[3],
                            mModelData[4], mModelData[5], mModelData[6], mModelData[7], 0 }};

    stream<< "Rotation: "<<mXRot<<" "<<mYRot<<" "<<mZRot<<"\n";
    stream<< "ModelIdx: "<<mModelIdx<<" ("<<id.data()<<")\n";
    stream<< "ActionFlags: 0x"<<std::hex<<std::setw(8)<<mActionFlags<<std::dec<<std::setw(0)<<"\n";
    stream<< "SoundId: "<<(int)mSoundId<<"\n";
}


void FlatObject::load(std::istream &stream, const osg::Vec3 &basepos)
{
    mTexture = VFS::read_le16(stream);
    mGender = VFS::read_le16(stream);
    mFactionId = VFS::read_le16(stream);
    mActionOffset = VFS::read_le32(stream);
    mUnknown = stream.get();

    osg::Vec3 pos = basepos + osg::Vec3(mXPos, mYPos, mZPos);
    if(mActionOffset > 0)
        loadAction(stream, mActionOffset, 0x02, 0, pos, osg::Vec3());

    size_t numframes = 0;
    osg::ref_ptr<osg::MatrixTransform> node(new osg::MatrixTransform());
    node->setNodeMask(Renderer::Mask_Flat);
    node->setUserData(new ObjectRef(mId));
    node->addChild(Resource::MeshManager::get().loadFlat(mTexture, true, &numframes));
    Renderer::get().getObjectRoot()->addChild(node);

    Renderer::get().setNode(mId, node);
    if(numframes > 1)
    {
        // Animation speed is hardcoded? Might it be specified somewhere else?
        Animated::get().allocate(mId, numframes, 1.0f/12.0f);
    }
    Placeable::get().setPoint(mId, pos);
}

void FlatObject::print(std::ostream &stream) const
{
    DF::ObjectBase::print(stream);

    stream<< "Texture: 0x"<<std::hex<<std::setw(4)<<mTexture<<std::dec<<std::setw(0)<<"\n";
    stream<< "Gender: 0x"<<std::hex<<std::setw(4)<<mGender<<std::dec<<std::setw(0)<<"\n";
    stream<< "FactionId: "<<mFactionId<<"\n";
    stream<< "Unknown: 0x"<<std::hex<<std::setw(2)<<(int)mUnknown<<std::setw(0)<<std::dec<<"\n";
}


DBlockHeader::DBlockHeader() { }
DBlockHeader::~DBlockHeader()
{
    if(!mModels.empty())
    {
        Renderer::get().remove(&*mModels.getIdList(), mModels.size());
        Placeable::get().deallocate(&*mModels.getIdList(), mModels.size());
    }
    if(!mFlats.empty())
    {
        Renderer::get().remove(&*mFlats.getIdList(), mFlats.size());
        Placeable::get().deallocate(&*mFlats.getIdList(), mFlats.size());
    }
}


void DBlockHeader::load(std::istream &stream, size_t blockid, float x, float z, size_t regnum, size_t locnum)
{
    mUnknown1 = VFS::read_le32(stream);
    mWidth = VFS::read_le32(stream);
    mHeight = VFS::read_le32(stream);
    mObjectRootOffset = VFS::read_le32(stream);
    mUnknown2 = VFS::read_le32(stream);
    stream.read(mModelData[0].data(), sizeof(mModelData));
    for(uint32_t &val : mUnknown3)
        val = VFS::read_le32(stream);

    mUnknownOffset = VFS::read_le32(stream);
    mUnknown4 = VFS::read_le32(stream);
    mUnknown5 = VFS::read_le32(stream);
    mUnknown6 = VFS::read_le32(stream);

    {
        // Seems to be one entry for each valid ModelData....
        int32_t offset = mUnknownOffset;
        while(stream.good() && offset > 0)
        {
            stream.seekg(offset);
            offset = VFS::read_le32(stream);
            mUnknownList.push_back(offset);
        }
    }

    stream.clear();
    stream.seekg(mObjectRootOffset);

    std::vector<int32_t> rootoffsets(mWidth*mHeight);
    for(int32_t &val : rootoffsets)
        val = VFS::read_le32(stream);

    osg::Vec3 basepos(x, 0.0f, z);
    for(int32_t offset : rootoffsets)
    {
        while(offset > 0)
        {
            stream.seekg(offset);
            int32_t next = VFS::read_le32(stream);
            /*int32_t prev =*/ VFS::read_le32(stream);

            int32_t x = VFS::read_le32(stream);
            int32_t y = VFS::read_le32(stream);
            int32_t z = VFS::read_le32(stream);
            uint8_t type = stream.get();
            uint32_t objoffset = VFS::read_le32(stream);

            if(type == ObjectType_Model)
            {
                stream.seekg(objoffset);
                ModelObject *model = mModels.insert(blockid|offset,
                    std::unique_ptr<ModelObject>(new ModelObject(blockid|offset, x, y, z))
                ).first->get();
                model->load(stream, mModelData, regnum, locnum, basepos);
            }
            else if(type == ObjectType_Flat)
            {
                stream.seekg(objoffset);
                FlatObject *flat = mFlats.insert(blockid|offset,
                    std::unique_ptr<FlatObject>(new FlatObject(blockid|offset, x, y, z))
                ).first->get();
                flat->load(stream, basepos);
            }

            offset = next;
        }
    }
}


ObjectBase *DBlockHeader::getObject(size_t id)
{
    {
        auto iter = mModels.find(id);
        if(iter != mModels.end())
            return iter->get();
    }
    {
        auto iter = mFlats.find(id);
        if(iter != mFlats.end())
            return iter->get();
    }
    return nullptr;
}

size_t DBlockHeader::getObjectByTexture(size_t texid) const
{
    for(const std::unique_ptr<FlatObject> &flat : mFlats)
    {
        if(flat->mTexture == texid)
            return flat->mId;
    }
    Log::get().stream(Log::Level_Error)<< "Failed to find Flat with texture 0x"<<std::setfill('0')<<std::setw(4)<<std::hex<<texid;
    return ~static_cast<size_t>(0);
}


void DBlockHeader::print(std::ostream &stream, int objtype) const
{
    stream<< "Unknown: 0x"<<std::hex<<std::setw(8)<<mUnknown1<<std::dec<<std::setw(0)<<"\n";
    stream<< "Width: "<<mWidth<<"\n";
    stream<< "Height: "<<mHeight<<"\n";
    stream<< "ObjectRootOffset: 0x"<<std::hex<<std::setw(8)<<mObjectRootOffset<<std::dec<<std::setw(0)<<"\n";
    stream<< "Unknown: 0x"<<std::hex<<std::setw(8)<<mUnknown2<<std::dec<<std::setw(0)<<"\n";
    stream<< "ModelData:"<<"\n";
    const uint32_t *unknown = mUnknown3.data();
    int idx = 0;
    for(const auto &id : mModelData)
    {
        if(id[0] != -1)
        {
            std::array<char,9> disp{{id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], 0}};
            stream<< " "<<idx<<": "<<disp.data()<<" 0x"<<std::hex<<std::setw(8)<<*unknown<<std::setw(0)<<std::dec<<"\n";
        }
        ++unknown;
        ++idx;
    }
    stream<< "UnknownOffset: 0x"<<std::hex<<std::setw(8)<<mUnknownOffset<<std::dec<<std::setw(0)<<"\n";
    stream<< "Unknown: 0x"<<std::hex<<std::setw(8)<<mUnknown4<<std::dec<<std::setw(0)<<"\n";
    stream<< "Unknown: 0x"<<std::hex<<std::setw(8)<<mUnknown5<<std::dec<<std::setw(0)<<"\n";
    stream<< "Unknown: 0x"<<std::hex<<std::setw(8)<<mUnknown6<<std::dec<<std::setw(0)<<"\n";

    auto iditer = mModels.getIdList();
    for(const std::unique_ptr<ModelObject> &model : mModels)
    {
        stream<< "**** Object 0x"<<std::hex<<std::setw(8)<<*iditer<<std::setw(0)<<std::dec<<" ****\n";
        model->print(stream);
        ++iditer;
    }
    iditer = mFlats.getIdList();
    for(const std::unique_ptr<FlatObject> &flat : mFlats)
    {
        stream<< "**** Object 0x"<<std::hex<<std::setw(8)<<*iditer<<std::setw(0)<<std::dec<<" ****\n";
        flat->print(stream);
        ++iditer;
    }
}

} // namespace DF

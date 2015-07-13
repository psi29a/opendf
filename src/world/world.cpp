
#include "world.hpp"

#include <sstream>
#include <iomanip>

#include <osgViewer/Viewer>
#include <osg/Light>

#include "components/vfs/manager.hpp"

#include "dblocks.hpp"
#include "cvars.hpp"
#include "log.hpp"


namespace
{

static const char gBlockIndexLabel[6] = { 'N', 'W', 'L', 'S', 'B', 'M' };

/* This is only stored temporarily */
struct DungeonHeader {
    struct Offset {
        uint32_t mOffset;
        uint16_t mIsDungeon;
        uint16_t mExteriorLocationId;
    };

    uint32_t mDungeonCount;
    std::vector<Offset> mOffsets;

    void load(std::istream &stream)
    {
        mDungeonCount = VFS::read_le32(stream);

        mOffsets.resize(mDungeonCount);
        for(Offset &offset : mOffsets)
        {
            offset.mOffset = VFS::read_le32(stream);
            offset.mIsDungeon = VFS::read_le16(stream);
            offset.mExteriorLocationId = VFS::read_le16(stream);
        }
    }
};

}

namespace DF
{

void LocationHeader::load(std::istream &stream)
{
    mDoorCount = VFS::read_le32(stream);

    mDoors.resize(mDoorCount);
    for(auto &door : mDoors)
    {
        door.mBuildingDataIndex = VFS::read_le16(stream);
        door.mNullValue = stream.get();
        door.mUnknownMask = stream.get();
        door.mUnknown1 = stream.get();
        door.mUnknown2 = stream.get();
    }

    mAlwaysOne1 = VFS::read_le32(stream);
    mNullValue1 = VFS::read_le16(stream);
    mNullValue2 = stream.get();
    mY = VFS::read_le32(stream);
    mNullValue3 = VFS::read_le32(stream);
    mX = VFS::read_le32(stream);
    mIsExterior = VFS::read_le16(stream);
    mNullValue4 = VFS::read_le16(stream);
    mUnknown1 = VFS::read_le32(stream);
    mUnknown2 = VFS::read_le32(stream);
    mAlwaysOne2 = VFS::read_le16(stream);
    mLocationId = VFS::read_le16(stream);
    mNullValue5 = VFS::read_le32(stream);
    mIsInterior = VFS::read_le16(stream);
    mExteriorLocationId = VFS::read_le32(stream);
    stream.read(reinterpret_cast<char*>(mNullValue6), sizeof(mNullValue6));
    stream.read(mLocationName, sizeof(mLocationName));
    stream.read(reinterpret_cast<char*>(mUnknown3), sizeof(mUnknown3));
}

LogStream& operator<<(LogStream &stream, const LocationHeader &loc)
{
    stream<<std::setfill('0');
    stream<< "  Door count: "<<loc.mDoorCount<<"\n";
    for(const LocationDoor &door : loc.mDoors)
    {
        stream<< "  Door "<<std::distance(loc.mDoors.data(), &door)<<":\n";
        stream<< "    BuildingDataIndex: "<<door.mBuildingDataIndex<<"\n";
        stream<< "    Null: "<<(int)door.mNullValue<<"\n";
        stream<< "    UnknownMask: 0x"<<std::hex<<std::setw(2)<<(int)door.mUnknownMask<<std::setw(0)<<std::dec<<"\n";
        stream<< "    Unknown: 0x"<<std::hex<<std::setw(2)<<(int)door.mUnknown1<<std::setw(0)<<std::dec<<"\n";
        stream<< "    Unknown: 0x"<<std::hex<<std::setw(2)<<(int)door.mUnknown2<<std::setw(0)<<std::dec<<"\n";
    }

    stream<< "  Always one: "<<loc.mAlwaysOne1<<"\n";
    stream<< "  Null: "<<loc.mNullValue1<<"\n";
    stream<< "  Null: "<<(int)loc.mNullValue2<<"\n";
    stream<< "  Y: "<<loc.mY<<"\n";
    stream<< "  Null: "<<loc.mNullValue3<<"\n";
    stream<< "  X: "<<loc.mX<<"\n";
    stream<< "  IsExterior: "<<loc.mIsExterior<<"\n";
    stream<< "  Null: "<<loc.mNullValue4<<"\n";
    stream<< "  Unknown: 0x"<<std::hex<<std::setw(8)<<loc.mUnknown1<<std::setw(0)<<std::dec<<"\n";
    stream<< "  Unknown: 0x"<<std::hex<<std::setw(8)<<loc.mUnknown2<<std::setw(0)<<std::dec<<"\n";
    stream<< "  Always one: "<<loc.mAlwaysOne2<<"\n";
    stream<< "  LocationID: "<<loc.mLocationId<<"\n";
    stream<< "  Null: "<<loc.mNullValue5<<"\n";
    stream<< "  IsInterior: "<<loc.mIsInterior<<"\n";
    stream<< "  ExteriorLocationID: "<<loc.mExteriorLocationId<<"\n";
    //uint8_t mNullValue6[26];
    stream<< "  LocationName: \""<<loc.mLocationName<<"\"\n";
    stream<< "  Unknown:";//<<std::hex<<std::setw(2);
    for(uint32_t unk : loc.mUnknown3)
        stream<< " 0x"<<std::hex<<std::setw(2)<<unk;
    stream<<std::setw(0)<<std::dec<<std::setfill(' ');

    return stream;
}


CCMD(dumparea)
{
    WorldIface::get().dumpArea();
}


World World::sWorld;
WorldIface &WorldIface::sInstance = World::sWorld;

World::World()
  : mCurrentRegion(nullptr)
  , mCurrentExterior(nullptr)
  , mCurrentDungeon(nullptr)
{
}

World::~World()
{
}


void World::initialize(osgViewer::Viewer *viewer)
{
    std::set<std::string> names = VFS::Manager::get().list("MAPNAMES.[0-9]*");
    if(names.empty()) throw std::runtime_error("Failed to find any regions");

    for(const std::string &name : names)
    {
        size_t pos = name.rfind('.');
        if(pos == std::string::npos)
            continue;
        std::string regstr = name.substr(pos+1);
        unsigned long regnum = std::stoul(regstr, nullptr, 10);

        /* Get names */
        VFS::IStreamPtr stream = VFS::Manager::get().open(name.c_str());
        if(!stream) throw std::runtime_error("Failed to open "+name);

        uint32_t mapcount = VFS::read_le32(*stream);
        if(mapcount == 0) continue;

        MapRegion region;
        region.mNames.resize(mapcount);
        for(std::string &mapname : region.mNames)
        {
            char mname[32];
            if(!stream->read(mname, sizeof(mname)) || stream->gcount() != sizeof(mname))
                throw std::runtime_error("Failed to read map names from "+name);
            mapname.assign(mname, sizeof(mname));
            size_t end = mapname.find('\0');
            if(end != std::string::npos)
                mapname.resize(end);
        }
        stream = nullptr;

        /* Get table data */
        std::string fname = "MAPTABLE."+regstr;
        stream = VFS::Manager::get().open(fname.c_str());
        if(!stream) throw std::runtime_error("Failed to open "+fname);

        region.mTable.resize(region.mNames.size());
        for(MapTable &maptable : region.mTable)
        {
            maptable.mMapId = VFS::read_le32(*stream);
            maptable.mUnknown1 = stream->get();
            maptable.mLongitudeType = VFS::read_le32(*stream);
            maptable.mLatitude = VFS::read_le16(*stream);
            maptable.mUnknown2 = VFS::read_le16(*stream);
            maptable.mUnknown3 = VFS::read_le32(*stream);
        }
        stream = nullptr;

        /* Get exterior data */
        fname = "MAPPITEM."+regstr;
        stream = VFS::Manager::get().open(fname.c_str());
        if(!stream) throw std::runtime_error("Failed to open "+fname);

        std::vector<uint32_t> extoffsets(region.mNames.size());
        for(uint32_t &offset : extoffsets)
            offset = VFS::read_le32(*stream);
        std::streamoff extbase_offset = stream->tellg();

        uint32_t *extoffset = extoffsets.data();
        region.mExteriors.resize(extoffsets.size());
        for(ExteriorLocation &extinfo : region.mExteriors)
        {
            stream->seekg(extbase_offset + *extoffset);
            extinfo.load(*stream);
            ++extoffset;
        }
        stream = nullptr;

        /* Get dungeon data */
        fname = "MAPDITEM."+regstr;
        stream = VFS::Manager::get().open(fname.c_str());
        if(!stream) throw std::runtime_error("Failed to open "+fname);

        DungeonHeader dheader;
        dheader.load(*stream);
        std::streamoff dbase_offset = stream->tellg();

        DungeonHeader::Offset *doffset = dheader.mOffsets.data();
        region.mDungeons.resize(dheader.mDungeonCount);
        for(DungeonInterior &dinfo : region.mDungeons)
        {
            stream->seekg(dbase_offset + doffset->mOffset);
            dinfo.load(*stream);
            if(dinfo.mExteriorLocationId != doffset->mExteriorLocationId)
                throw std::runtime_error("Dungeon exterior location id mismatch for "+std::string(dinfo.mLocationName)+": "+
                    std::to_string(dinfo.mExteriorLocationId)+" / "+std::to_string(doffset->mExteriorLocationId));
            ++doffset;
        }
        stream = nullptr;

        if(regnum >= mRegions.size()) mRegions.resize(regnum+1);
        mRegions[regnum] = std::move(region);
    }

    mViewer = viewer;

    mViewer->setLightingMode(osg::View::HEADLIGHT);
    osg::ref_ptr<osg::Light> light(new osg::Light());
    light->setAmbient(osg::Vec4(0.5f, 0.5f, 0.5f, 1.0f));
    mViewer->setLight(light);
}

void World::deinitialize()
{
    mDungeon.clear();
    mViewer = nullptr;
}


void World::loadDungeonByExterior(int regnum, int extid)
{
    const MapRegion &region = mRegions.at(regnum);
    const ExteriorLocation &extloc = region.mExteriors.at(extid);
    for(const DungeonInterior &dinfo : region.mDungeons)
    {
        if(extloc.mLocationId != dinfo.mExteriorLocationId)
            continue;

        mDungeon.clear();
        mCurrentRegion = &region;
        mCurrentExterior = &extloc;
        mCurrentDungeon = &dinfo;

        Log::get().stream()<< "Entering "<<dinfo.mLocationName;
        for(const DungeonBlock &block : dinfo.mBlocks)
        {
            std::stringstream sstr;
            sstr<< std::setfill('0')<<std::setw(8)<< block.mBlockIdx<<".RDB";
            std::string name = sstr.str();
            name.front() = gBlockIndexLabel[block.mBlockPreIndex];

            VFS::IStreamPtr stream = VFS::Manager::get().open(name.c_str());
            if(!stream) throw std::runtime_error("Failed to open "+name);

            mDungeon.push_back(DBlockHeader());
            mDungeon.back().load(*stream);
        }

        osg::Group *root = mViewer->getSceneData()->asGroup();
        for(size_t i = 0;i < mDungeon.size();++i)
            mDungeon[i].buildNodes(root, dinfo.mBlocks[i].mX, dinfo.mBlocks[i].mZ);
        break;
    }
}


void World::dumpArea() const
{
    LogStream stream(Log::get().stream());
    if(mCurrentDungeon)
    {
        int regnum = std::distance(mRegions.data(), mCurrentRegion);
        stream<< "Current region index: "<<regnum<<"\n";
        int mapnum = std::distance(mCurrentRegion->mExteriors.data(), mCurrentExterior);
        stream<< "Current exterior index: "<<mapnum<<"\n";
        stream<< "Current Dungeon:\n";
        stream<< *mCurrentDungeon;
    }
    else if(mCurrentExterior)
    {
        int regnum = std::distance(mRegions.data(), mCurrentRegion);
        stream<< "Current region index: "<<regnum<<"\n";
        stream<< "Current Exterior:\n";
        stream<< static_cast<const LocationHeader&>(*mCurrentExterior);
    }
    else if(mCurrentRegion)
    {
        int regnum = std::distance(mRegions.data(), mCurrentRegion);
        stream<< "Current region index "<<regnum;
    }
    else
        stream<< "Not in a region";
}

} // namespace DF

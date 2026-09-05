
#include <cstdint>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include "world.hpp"

#include <sstream>
#include <iomanip>
#include <array>

#include <osgViewer/Viewer>
#include <osg/Light>
#include <osg/Uniform>
#include <osg/StateSet>
#include <osg/Quat>

#include "components/vfs/manager.hpp"

#include "render/renderer.hpp"
#include "render/pipeline.hpp"
#include "class/animated.hpp"
#include "class/placeable.hpp"
#include "class/activator.hpp"
#include "actions/linker.hpp"
#include "actions/mover.hpp"
#include "actions/door.hpp"
#include "actions/exitdoor.hpp"
#include "actions/unknown.hpp"
#include "gui/iface.hpp"
#include "mblocks.hpp"
#include "dblocks.hpp"
#include "cvars.hpp"
#include "log.hpp"


namespace
{

static const size_t InvalidHandle = ~static_cast<size_t>(0);

static const std::array<char,6> gBlockIndexLabel{{ 'N', 'W', 'L', 'S', 'B', 'M' }};

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
    mX = VFS::read_le32(stream);
    mNullValue3 = VFS::read_le32(stream);
    mY = VFS::read_le32(stream);
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
    stream<< "  X: "<<loc.mX<<"\n";
    stream<< "  Null: "<<loc.mNullValue3<<"\n";
    stream<< "  Y: "<<loc.mY<<"\n";
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

CCMD(dumpblocks)
{
    WorldIface::get().dumpBlocks();
}


CCMD(warp)
{
    if(params.empty())
    {
        Log::get().stream(Log::Level_Error)<< "Usage: warp <region index> <location index>";
        return;
    }

    size_t regnum = ~static_cast<size_t>(0);
    size_t mapnum = ~static_cast<size_t>(0);

    if(params[0] >= '0' && params[0] <= '9')
    {
        char *next = nullptr;
        regnum = strtoul(params.c_str(), &next, 10);
        if(!next || *next != ' ')
        {
            Log::get().stream(Log::Level_Error)<< "Invalid region parameter: "<<params;
            return;
        }
        mapnum = strtoul(next, &next, 10);
        if(next && *next != '\0')
        {
            Log::get().stream(Log::Level_Error)<< "Invalid location parameter: "<<params;
            return;
        }
    }
    else
    {
        if(!WorldIface::get().getExteriorByName(params, regnum, mapnum))
        {
            Log::get().stream(Log::Level_Error)<< "Failed to find exterior \""<<params<<"\"";
            return;
        }
    }

    try {
        WorldIface::get().loadExterior(regnum, mapnum);
    }
    catch(std::exception &e) {
        Log::get().stream(Log::Level_Error)<< "Exception: "<<e.what();
        return;
    }
}

CCMD(dwarp)
{
    if(params.empty())
    {
        WorldIface::get().loadCurrentExteriorDungeon();
        return;
    }

    try {
        char *next = nullptr;
        size_t regnum = strtoul(params.c_str(), &next, 10);
        if(!next || *next != ' ')
        {
            Log::get().stream(Log::Level_Error)<< "Invalid region parameter: "<<params;
            return;
        }
        size_t mapnum = strtoul(next, &next, 10);
        if(next && *next != '\0')
        {
            Log::get().stream(Log::Level_Error)<< "Invalid location parameter: "<<params;
            return;
        }

        WorldIface::get().loadDungeonByExterior(regnum, mapnum);
    }
    catch(std::exception &e) {
        Log::get().stream(Log::Level_Error)<< "Exception: "<<e.what();
        return;
    }
}


CVAR(CVarBool, g_introspect, false);

// Lighting tuning knobs. These are percentages because the cvar system has no
// float type; Daggerfall Unity exposes the same first two as 0..1 floats
// (DungeonAmbientLightScale, PlayerTorchLightScale) and hardcodes the third as
// m_Intensity on its dungeon light prefab.
CVAR(CVarInt, r_ambientscale,   100, 0, 400);   // scales whichever ambient is active
CVAR(CVarInt, r_torchscale,     100, 0, 400);   // the player torch
CVAR(CVarInt, r_lightintensity,  80, 0, 400);   // RDB point lights; DFU uses 0.8

CCMD(relight)
{
    WorldIface::get().applyLightSettings();
}


World World::sWorld;
WorldIface &WorldIface::sInstance = World::sWorld;

World::World()
  : mCurrentRegion(nullptr)
  , mCurrentExterior(nullptr)
  , mCurrentDungeon(nullptr)
  , mCurrentSelection(InvalidHandle)
  , mFirstStart(true)
{
}

World::~World()
{
}


void World::initialize(osgViewer::Viewer *viewer, osg::Group *sceneroot)
{
    std::set<std::string> names = VFS::Manager::get().list("MAPNAMES.[0-9]*");
    if(names.empty()) throw std::runtime_error("Failed to find any regions");

    VFS::IStreamPtr stream;
    for(const std::string &name : names)
    {
        size_t pos = name.rfind('.');
        if(pos == std::string::npos)
            continue;
        std::string regstr = name.substr(pos+1);
        unsigned long regnum = std::stoul(regstr, nullptr, 10);

        /* Get names */
        stream = VFS::Manager::get().open(name.c_str());
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

    loadPakList("CLIMATE.PAK", mClimates);
    loadPakList("POLITIC.PAK", mPolitics);

    mViewer = viewer;
    Renderer::get().setObjectRoot(sceneroot);
}

void World::deinitialize()
{
    mExterior.clear();
    mDungeon.clear();
    mSunLight = nullptr;
    // The torch belongs to the pipeline's light pass, which deinitialize()
    // drops. Holding a stale pointer would make the next setSunLight() skip
    // creating a replacement and leave update() moving a detached node.
    mTorchLight = nullptr;
    mCurrentBlock = -1;
    mInCastleBlock = false;
    Renderer::get().setObjectRoot(nullptr);
    mViewer = nullptr;
}


void World::loadPakList(std::string&& fname, std::vector<PakArray> &paklist)
{
    VFS::IStreamPtr stream = VFS::Manager::get().open(fname.c_str());
    if(!stream) throw std::runtime_error("Failed to open "+fname);

    size_t rownum = 0;
    std::map<size_t,size_t> offsets_rows;
    offsets_rows[VFS::read_le32(*stream)] = rownum++;
    while((size_t)stream->tellg() < offsets_rows.begin()->first)
        offsets_rows[VFS::read_le32(*stream)] = rownum++;

    auto iter = offsets_rows.begin();
    while(iter != offsets_rows.end())
    {
        auto next = std::next(iter);

        PakArray pak;
        while((next != offsets_rows.end() && (size_t)stream->tellg() < next->first) ||
              (next == offsets_rows.end() && stream->peek() != std::istream::traits_type::eof()))
        {
            uint16_t count = VFS::read_le16(*stream);
            uint8_t val = stream->get();
            pak.push_back(std::make_pair(count, val));
        }

        if(iter->second >= paklist.size())
            paklist.resize(iter->second+1);
        paklist[iter->second] = std::move(pak);

        iter = next;
    }
}

uint8_t World::getPakListValue(const std::vector<PakArray> &paklist, size_t x, size_t y)
{
    /* As mentioned on UESP: http://uesp.net/wiki/Daggerfall:WOODS.WLD_format
     *
     * "the valid domain for each axis is defined as:
     * Dom(X) = (51200, 32389120)
     * Dom(Y) = (-80, 1228)
     * Dom(Z) = (40961, 16332801)"
     *
     * However, there appears to be a mistake in that the Dom(X) and Dom(Z)
     * values correspond to the ExteriorLocation's X and Y values, where those
     * are the region map's Longitude and Latitude value*256 + n (where n is
     * 0...255). Additionally, the latitude increases while going north. Thus,
     * the correct domain for the longitude/latitude is:
     *
     * Dom(X) = [200, 126520]
     * Dom(Z) = [63800, 160]
     *
     * However, I'm not sure if this range correctly maps to the world/climate/
     * etc map extents.
     */
    if(x <= 200) x = 0;
    else x = size_t(uint64_t(x-200) * 1000 / (126520-200));

    if(y <= 160) y = 499;
    else y = 499 - size_t(uint64_t(y-160) * 499 / (63800-160));

    uint8_t value = 0;
    const PakArray &pak = paklist[std::min(y, paklist.size()-1)];
    for(const auto &entry : pak)
    {
        value = entry.second;
        if(x < entry.first)
            break;
        x -= entry.first;
    }
    return value;
}


bool World::getExteriorByName(const std::string &name, size_t &regnum, size_t &mapnum) const
{
    for(const MapRegion &region : mRegions)
    {
        auto iter = std::find(region.mNames.begin(), region.mNames.end(), name);
        if(iter != region.mNames.end())
        {
            regnum = std::distance(mRegions.data(), &region);
            mapnum = std::distance(region.mNames.begin(), iter);
            return true;
        }
    }
    return false;
}

// The "global light" quad is always present: dir_light.frag computes
// max(ambient, diffuse*N.L), so with a black diffuse it contributes just the
// flat ambient term -- and it is the only thing that consumes ambient_color.
// Outdoors that quad is a real sun over a bright sky ambient; underground the
// sun goes black and the ambient drops to a dungeon level, leaving the RDB's
// point lights to do the actual lighting.
void World::setSunLight(bool enable)
{
    RenderPipeline &pipeline = RenderPipeline::get();

    if(!mSunLight.valid())
    {
        osg::Vec3f lightDir(70.f, -100.f, 10.f);
        lightDir.normalize();
        mSunLight = pipeline.createDirectionalLight();
        mSunLight->getOrCreateStateSet()->addUniform(
            new osg::Uniform("light_direction", lightDir));
    }

    mSunEnabled = enable;
    applyLightSettings();
}

// Which dungeon block the camera is in. Only the castle flag is consumed so
// far, and only on a change, so this costs a short scan on the frames where
// you cross a block boundary and nothing on the rest.
void World::updateCurrentBlock()
{
    if(mDungeon.empty())
    {
        if(mCurrentBlock != -1) { mCurrentBlock = -1; mInCastleBlock = false; }
        return;
    }

    // Undo the negate-and-flip that built mCameraPos, to get back to the
    // object space the blocks were placed in.
    const osg::Vec3f p(-mCameraPos.x(), mCameraPos.y(), mCameraPos.z());

    if(mCurrentBlock >= 0 && mCurrentBlock < (int)mDungeon.size()
       && mDungeon[mCurrentBlock]->contains(p))
        return;

    for(size_t i = 0;i < mDungeon.size();++i)
    {
        if(!mDungeon[i]->contains(p))
            continue;
        mCurrentBlock = (int)i;
        if(mInCastleBlock != mDungeon[i]->mCastleBlock)
        {
            mInCastleBlock = mDungeon[i]->mCastleBlock;
            applyLightSettings();
        }
        return;
    }
}

// Everything the lighting cvars touch, in one place so the `relight` console
// command can re-apply them without reloading the world.
void World::applyLightSettings()
{
    RenderPipeline &pipeline = RenderPipeline::get();
    const bool enable = mSunEnabled;

    const float ambientScale = *r_ambientscale / 100.0f;
    const float torchScale   = *r_torchscale / 100.0f;
    const float intensity    = *r_lightintensity / 100.0f;

    if(mSunLight.valid())
    {
        osg::StateSet *ss = mSunLight->getOrCreateStateSet();
        osg::Vec4f sun = enable ? osg::Vec4f(1.0f, 0.988f, 0.933f, 1.0f)
                                : osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
        ss->getOrCreateUniform("diffuse_color", osg::Uniform::FLOAT_VEC4)->set(sun);
        ss->getOrCreateUniform("specular_color", osg::Uniform::FLOAT_VEC4)->set(
            enable ? osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f) : osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
    }

    // Ambient matches Daggerfall Unity's PlayerAmbientLight: 0.12 underground,
    // but 0.58 inside a castle block -- Daggerfall scales ambient up a long way
    // in the main-story castles, and without this Wayrest is nearly five times
    // too dark.
    // Exterior ambient is the old eyeball-tuned value converted to linear
    // (c^2.2), so it looks as it did before lighting moved to linear space
    // rather than being washed out by the encode. Still ours, not DFU's, and
    // still fixed until there is a time of day to interpolate over.
    osg::Vec4f ambient = enable ? osg::Vec4f(0.255f, 0.267f, 0.358f, 1.0f)
                    : (mInCastleBlock ? osg::Vec4f(0.580f, 0.580f, 0.580f, 1.0f)
                                      : osg::Vec4f(0.120f, 0.120f, 0.120f, 1.0f));
    ambient = osg::Vec4f(ambient.r()*ambientScale, ambient.g()*ambientScale,
                         ambient.b()*ambientScale, 1.0f);
    pipeline.getLightingStateSet()->getUniform("ambient_color")->set(ambient);

    // RDB point lights set no colour of their own, so they inherit this one
    // from the light pass -- which makes it the single place to scale them all.
    // DFU's dungeon light prefab uses m_Intensity 0.8.
    osg::StateSet *lss = pipeline.getLightingStateSet();
    lss->getUniform("diffuse_color")->set(osg::Vec4f(intensity, intensity, intensity, 1.0f));
    lss->getUniform("specular_color")->set(osg::Vec4f(intensity, intensity, intensity, 1.0f));

    // Player torch, matching Daggerfall Unity's EnablePlayerTorch: a white
    // point light on the player, intensity 1.0, range 6 Unity units -- which
    // is 240 Daggerfall units at MeshReader.GlobalScale (0.025). Without it
    // the RDB lights alone leave dungeons far darker than DFU's, even though
    // our ambient constant matches theirs exactly.
    // ponytail: always on underground. DFU gates it on a held light source
    // and dims it as the item burns down; that needs an inventory we don't
    // have yet, and a torch you can't lose beats a dungeon you can't see.
    if(!mTorchLight.valid())
        mTorchLight = pipeline.createPointLight(-mCameraPos, 240.0f);
    osg::StateSet *tss = mTorchLight->getOrCreateStateSet();
    osg::Vec4f torch = enable ? osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f)
                              : osg::Vec4f(torchScale, torchScale, torchScale, 1.0f);
    tss->getOrCreateUniform("diffuse_color", osg::Uniform::FLOAT_VEC4)->set(torch);
    tss->getOrCreateUniform("specular_color", osg::Uniform::FLOAT_VEC4)->set(torch);
}


void World::loadExterior(int regnum, int extid)
{
    const MapRegion &region = mRegions.at(regnum);
    const ExteriorLocation &extloc = region.mExteriors.at(extid);

    mExterior.clear();
    mDungeon.clear();
    mCurrentRegion = &region;
    mCurrentExterior = &extloc;
    mCurrentDungeon = nullptr;
    mCurrentSelection = InvalidHandle;

    setSunLight(true);

    uint8_t climate = getClimateValue(extloc.mX/256, extloc.mY/256);
    Log::get().stream()<< "Climate "<<(int)climate;

    Log::get().stream()<< "Entering "<<extloc.mLocationName;
    size_t count = extloc.mWidth * extloc.mHeight;
    size_t startobj = InvalidHandle;

    mExterior.reserve(count);
    for(size_t i = 0;i < count;++i)
    {
        std::string name = extloc.getMapBlockName(i, regnum);
        if(!VFS::Manager::get().exists(name.c_str()))
        {
            Log::get().stream()<< name<<" does not exist";
            name.erase(4);
            name[4] = '*';
            std::set<std::string> list = VFS::Manager::get().list(name.c_str());
            if(list.empty()) name.clear();
            else name = *list.begin();
        }

        VFS::IStreamPtr stream = VFS::Manager::get().open(name.c_str());
        if(!stream) throw std::runtime_error("Failed to open "+name);

        int x = i%extloc.mWidth;
        int y = i/extloc.mWidth;

        mExterior.push_back(std::unique_ptr<MBlockHeader>(new MBlockHeader()));
        mExterior.back()->load(*stream, climate, i<<24, x*4096.0f, y*4096.0f);

        if(startobj != InvalidHandle)
            continue;

        startobj = mExterior.back()->getObjectByTexture(Marker_EnterID);
        if(startobj == InvalidHandle)
            startobj = mExterior.back()->getObjectByTexture(Marker_StartID);

        if(startobj != InvalidHandle)
        {
            Position pos = Placeable::get().getPos(startobj);
            mCameraPos = osg::componentMultiply(
                -pos.mPoint, osg::Vec3f(1.0f, -1.0f, -1.0f)
            );
        }
    }
    if(startobj == InvalidHandle)
    {
        Log::get().message("Failed to find enter or start markers", Log::Level_Error);
        mCameraPos = osg::Vec3f(-2048.0f, 0.0f, -2048.0f);
    }
    mCameraRot = osg::Vec3f(0.0f, 1024.0f, 0.0f);
}

void World::loadDungeonByExterior(int regnum, int extid)
{
    const MapRegion &region = mRegions.at(regnum);
    const ExteriorLocation &extloc = region.mExteriors.at(extid);
    for(const DungeonInterior &dinfo : region.mDungeons)
    {
        if(extloc.mLocationId != dinfo.mExteriorLocationId)
            continue;

        mExterior.clear();
        mDungeon.clear();
        mCurrentRegion = &region;
        mCurrentExterior = &extloc;
        mCurrentDungeon = &dinfo;
        mCurrentSelection = InvalidHandle;

        // Cleared *before* setSunLight, which applies ambient using them: a
        // stale castle flag from the dungeon we just left would otherwise pick
        // the 0.58 ambient, and updateCurrentBlock() only re-applies on a
        // change, so a non-castle start block would leave it stuck there.
        mCurrentBlock = -1;
        mInCastleBlock = false;

        setSunLight(false);

        uint8_t climate = getClimateValue(extloc.mX/256, extloc.mY/256);
        Log::get().stream()<< "Climate "<<(int)climate;

        Log::get().stream()<< "Entering "<<dinfo.mLocationName;
        mDungeon.reserve(dinfo.mBlocks.size());
        for(const DungeonBlock &block : dinfo.mBlocks)
        {
            std::stringstream sstr;
            sstr<< std::setfill('0')<<std::setw(8)<< block.mBlockIdx<<".RDB";
            std::string name = sstr.str();
            name.front() = gBlockIndexLabel.at(block.mBlockPreIndex);

            VFS::IStreamPtr stream = VFS::Manager::get().open(name.c_str());
            if(!stream) throw std::runtime_error("Failed to open "+name);

            mDungeon.push_back(std::unique_ptr<DBlockHeader>(new DBlockHeader()));
            mDungeon.back()->load(*stream, std::distance(dinfo.mBlocks.data(), &block)<<24,
                                  block.mX*2048.0f, block.mZ*2048.0f, regnum, extid);

            if(block.mStartBlock)
            {
                size_t startobj = InvalidHandle;
                if(mFirstStart)
                {
                    mFirstStart = false;
                    startobj = mDungeon.back()->getObjectByTexture(Marker_EnterID);
                }
                if(startobj == InvalidHandle)
                    startobj = mDungeon.back()->getObjectByTexture(Marker_StartID);

                if(startobj == InvalidHandle)
                    mCameraPos = osg::Vec3f(0.0f, 0.0f, 0.0f);
                else
                {
                    Position pos = Placeable::get().getPos(startobj);
                    mCameraPos = osg::componentMultiply(
                        -(pos.mPoint + osg::Vec3f(block.mX*2048.0f, 0.0f, block.mZ*2048.0f)),
                        osg::Vec3f(1.0f, -1.0f, -1.0f)
                    );
                }
                mCameraRot = osg::Vec3f(0.0f, 1024.0f, 0.0f);
            }
        }
        break;
    }
}

void World::loadCurrentExteriorDungeon()
{
    if(mCurrentDungeon)
        Log::get().message("Already in a dungeon");
    else if(!mCurrentRegion)
        Log::get().message("Not currently in a region");
    else if(!mCurrentExterior)
        Log::get().message("Not currently in an exterior");
    else
        loadDungeonByExterior(std::distance((const MapRegion*)mRegions.data(), mCurrentRegion),
                              std::distance(mCurrentRegion->mExteriors.data(), mCurrentExterior));
}


void World::move(float xrel, float yrel, float zrel)
{
    osg::Matrixf matf(osg::Matrixf::rotate(
                                    0.0f, osg::Vec3f(0.0f, 0.0f, 1.0f),
         mCameraRot.y()*3.14159f/1024.0f, osg::Vec3f(0.0f, 1.0f, 0.0f),
        -mCameraRot.x()*3.14159f/1024.0f, osg::Vec3f(1.0f, 0.0f, 0.0f)
    ));
    mCameraPos += matf*osg::Vec3f(xrel, yrel, zrel);
}

void World::rotate(float xrel, float yrel)
{
    mCameraRot.x() = std::min(std::max(mCameraRot.x()+xrel, -511.0f), 511.0f);
    mCameraRot.y() += yrel;
}


void World::update(float timediff)
{
    GuiIface::Mode guimode = GuiIface::get().getMode();

    UnknownAction::get().update();
    if(guimode <= GuiIface::Mode_Cursor)
    {
        ExitDoor::get().update();
        Linker::get().update();
        Mover::get().update(timediff);
        Door::get().update(timediff);
        Animated::get().update(timediff);
    }

    Renderer::get().update();

    osg::Matrixf matf(osg::Matrixf::rotate(
                                    0.0f, osg::Vec3f(0.0f, 0.0f, 1.0f),
         mCameraRot.y()*3.14159f/1024.0f, osg::Vec3f(0.0f, 1.0f, 0.0f),
        -mCameraRot.x()*3.14159f/1024.0f, osg::Vec3f(1.0f, 0.0f, 0.0f)
    ));
    matf.preMultTranslate(mCameraPos);
    mViewer->getCamera()->setViewMatrix(matf);

    // mCameraPos is the negated eye position, flipped in y/z the same way
    // LightObject::load flips an RDB light, so the torch sits at -mCameraPos.
    if(mTorchLight.valid())
        mTorchLight->getStateSet()->getUniform("light_position")->set(-mCameraPos);

    updateCurrentBlock();

    if(guimode == GuiIface::Mode_Game)
        mCurrentSelection = castCameraToViewportRay(0.5f, 0.5f, 1024.0f, false);
    else
    {
        float x, y;
        GuiIface::get().getMousePosition(x, y);
        mCurrentSelection = castCameraToViewportRay(x, y, 1024.0f, false);
    }
    if(mCurrentSelection == InvalidHandle || !*g_introspect)
        GuiIface::get().updateStatus(std::string());
    else
    {
        std::stringstream sstr;
        if(!mExterior.empty())
        {
            MBlockHeader *block = mExterior.at(mCurrentSelection>>24).get();
            const MObjectBase *obj = block->getObject(mCurrentSelection);
            if(!obj)
                sstr<< "Failed to lookup object 0x"<<std::hex<<std::setfill('0')<<std::setw(8)<<mCurrentSelection;
            else
            {
                sstr<<std::setfill('0');
                obj->print(sstr);
            }
        }
        else
        {
            DBlockHeader *block = mDungeon.at(mCurrentSelection>>24).get();
            const ObjectBase *obj = block->getObject(mCurrentSelection);
            if(!obj)
                sstr<< "Failed to lookup object 0x"<<std::hex<<std::setfill('0')<<std::setw(8)<<mCurrentSelection;
            else
            {
                sstr<<std::setfill('0');
                obj->print(sstr);
            }
        }
        GuiIface::get().updateStatus(sstr.str());
    }
}

void World::activate()
{
    if(mCurrentSelection != InvalidHandle)
        Activator::get().activate(mCurrentSelection);
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
        int mapnum = std::distance(mCurrentRegion->mExteriors.data(), mCurrentExterior);
        stream<< "Current exterior index: "<<mapnum<<"\n";
        stream<< "Current Exterior:\n";
        stream<< *mCurrentExterior;
    }
    else if(mCurrentRegion)
    {
        int regnum = std::distance(mRegions.data(), mCurrentRegion);
        stream<< "Current region index "<<regnum;
    }
    else
        stream<< "Not in a region";
}

void World::dumpBlocks() const
{
    std::stringstream sstr;
    sstr.fill('0');

    int i = 0;
    for(const std::unique_ptr<DBlockHeader> &block : mDungeon)
    {
        sstr<< "****** DBlock "<<i<<" ******\n";
        block->print(sstr);
        ++i;
    }
    i = 0;
    for(const std::unique_ptr<MBlockHeader> &block : mExterior)
    {
        sstr<< "****** MBlock "<<i<<" ******\n";
        block->print(sstr);
        ++i;
    }

    Log::get().message(sstr.str());
}


size_t World::castCameraToViewportRay(const float vpX, const float vpY, float maxDistance, bool ignoreFlats)
{
    osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector(new osgUtil::LineSegmentIntersector(
        osgUtil::LineSegmentIntersector::PROJECTION, vpX*2.f - 1.f, vpY*-2.f + 1.f
    ));

    osg::Vec3d dist = osg::Vec3d(0.0f,0.0f,-maxDistance) *
                      RenderPipeline::get().getProjectionMatrix();

    osg::Vec3d end = intersector->getEnd();
    end.z() = dist.z();
    intersector->setEnd(end);
    intersector->setIntersectionLimit(osgUtil::LineSegmentIntersector::LIMIT_NEAREST);

    osgUtil::IntersectionVisitor intersectionVisitor(intersector);
    int mask = intersectionVisitor.getTraversalMask();
    mask &= ~(Renderer::Mask_RTT | Renderer::Mask_UI | Renderer::Mask_Light);
    if(ignoreFlats) mask &= ~(Renderer::Mask_Flat);

    intersectionVisitor.setTraversalMask(mask);

    mViewer->getCamera()->accept(intersectionVisitor);

    size_t result = InvalidHandle;
    if(intersector->containsIntersections())
    {
        osgUtil::LineSegmentIntersector::Intersection intersection = intersector->getFirstIntersection();

        ObjectRef *ref = nullptr;
        for(auto it = intersection.nodePath.cbegin();it != intersection.nodePath.cend();++it)
        {
            osg::Referenced *userData = (*it)->getUserData();
            if(!userData) continue;

            ref = dynamic_cast<ObjectRef*>(userData);
            if(ref) break;
        }

        if(ref)
            result = ref->getId();
    }

    return result;
}

} // namespace DF

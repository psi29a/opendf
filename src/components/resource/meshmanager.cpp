
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <iterator>
#include "meshmanager.hpp"

#include <osg/Node>
#include <osg/MatrixTransform>
#include <osg/Billboard>
#include <osg/Geometry>
#include <osg/Texture>
#include <osg/AlphaFunc>
#include <osgDB/ReadFile>

#include "components/dfosg/meshloader.hpp"

#include "texturemanager.hpp"


namespace Resource
{

MeshManager MeshManager::sManager;

MeshManager::MeshManager()
{
}

MeshManager::~MeshManager()
{
}


void MeshManager::initialize()
{
}

void MeshManager::deinitialize()
{
    mStateSetCache.clear();
    mTerrainCache.clear();
    mFlatCache.clear();
    mModelCache.clear();
    mTerrainProgram = nullptr;
    mFlatProgram = nullptr;
    mModelProgram = nullptr;
}


osg::ref_ptr<osg::Node> MeshManager::get(size_t idx)
{
    /* Not sure if this cache is a good idea since it shares the whole model
     * tree. OSG can parent the same sub-tree to multiple points, which should
     * be okay as long as the individual sub-trees don't need changing.
     */
    auto iter = mModelCache.find(idx);
    if(iter != mModelCache.end())
    {
        osg::ref_ptr<osg::Node> node;
        if(iter->second.lock(node))
            return node;
    }

    if(!mModelProgram)
    {
        mModelProgram = new osg::Program();
        mModelProgram->addShader(osgDB::readShaderFile(osg::Shader::VERTEX, "shaders/object.vert"));
        mModelProgram->addShader(osgDB::readShaderFile(osg::Shader::FRAGMENT, "shaders/object.frag"));
    }

    DFOSG::Mesh *mesh = DFOSG::MeshLoader::get().load(idx);

    osg::ref_ptr<osg::Geode> geode(new osg::Geode());
    for(auto iter = mesh->getPlanes().begin();iter != mesh->getPlanes().end();)
    {
        osg::ref_ptr<osg::Vec3Array> vtxs(new osg::Vec3Array());
        osg::ref_ptr<osg::Vec3Array> nrms(new osg::Vec3Array());
        osg::ref_ptr<osg::Vec3Array> binrms(new osg::Vec3Array());
        osg::ref_ptr<osg::Vec2Array> texcrds(new osg::Vec2Array());
        osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray());
        osg::ref_ptr<osg::DrawElementsUShort> idxs(new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLES));
        uint16_t texid = iter->getTextureId();

        osg::ref_ptr<osg::Texture> tex = TextureManager::get().getTexture(texid);
        float width = tex->getTextureWidth();
        float height = tex->getTextureHeight();

        do {
            const std::vector<DFOSG::MdlPlanePoint> &pts = iter->getPoints();
            size_t last_total = vtxs->size();

            vtxs->resize(last_total + pts.size());
            nrms->resize(last_total + pts.size());
            binrms->resize(last_total + pts.size());
            texcrds->resize(last_total + pts.size());
            colors->resize(last_total + pts.size());
            idxs->resize((last_total + pts.size() - 2) * 3);

            size_t j = last_total;
            for(const DFOSG::MdlPlanePoint &pt : pts)
            {
                uint32_t vidx = pt.getIndex();

                (*vtxs)[j].x() = mesh->getPoints()[vidx].x() / 256.0f;
                (*vtxs)[j].y() = mesh->getPoints()[vidx].y() / 256.0f;
                (*vtxs)[j].z() = mesh->getPoints()[vidx].z() / 256.0f;

                (*nrms)[j].x() = iter->getNormal().x() / 256.0f;
                (*nrms)[j].y() = iter->getNormal().y() / 256.0f;
                (*nrms)[j].z() = iter->getNormal().z() / 256.0f;

                (*binrms)[j].x() = iter->getBinormal().x() / 256.0f;
                (*binrms)[j].y() = iter->getBinormal().y() / 256.0f;
                (*binrms)[j].z() = iter->getBinormal().z() / 256.0f;

                (*texcrds)[j].x() = pt.u() / width;
                (*texcrds)[j].y() = pt.v() / height;

                (*colors)[j] = osg::Vec4ub(255, 255, 255, 255);

                if(j >= last_total+2)
                {
                    (*idxs)[(j-2)*3 + 0] = last_total;
                    (*idxs)[(j-2)*3 + 1] = j-1;
                    (*idxs)[(j-2)*3 + 2] = j;
                }

                ++j;
            }
        } while(++iter != mesh->getPlanes().end() && iter->getTextureId() == texid);

        osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject());
        vtxs->setVertexBufferObject(vbo);
        nrms->setVertexBufferObject(vbo);
        texcrds->setVertexBufferObject(vbo);
        colors->setVertexBufferObject(vbo);
        colors->setNormalize(true);

        osg::ref_ptr<osg::ElementBufferObject> ebo(new osg::ElementBufferObject());
        idxs->setElementBufferObject(ebo);

        osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);
        geometry->setVertexArray(vtxs);
        geometry->setNormalArray(nrms, osg::Array::BIND_PER_VERTEX);
        geometry->setTexCoordArray(1, binrms, osg::Array::BIND_PER_VERTEX);
        geometry->setTexCoordArray(0, texcrds, osg::Array::BIND_PER_VERTEX);
        geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);

        geometry->addPrimitiveSet(idxs);

        /* Cache the stateset used for this texture, so it can be reused for
         * multiple models (should help OSG batch together objects with similar
         * state).
         */
        auto &stateiter = mStateSetCache[texid];
        osg::ref_ptr<osg::StateSet> ss;
        if(stateiter.lock(ss) && ss)
            geometry->setStateSet(ss);
        else
        {
            ss = geometry->getOrCreateStateSet();
            ss->setAttributeAndModes(mModelProgram);
            ss->addUniform(new osg::Uniform("diffuseTex", 0));
            ss->setTextureAttribute(0, tex);
            stateiter = ss;
        }

        geode->addDrawable(geometry);
    }

    mModelCache[idx] = osg::ref_ptr<osg::Node>(geode);
    return geode;
}

namespace
{

// Flats that emit their own light rather than merely reflecting it: fires,
// torches, braziers, glowing creatures. Daggerfall stores no flag for this, so
// like Daggerfall Unity (TextureReader.cs emissiveTextures) it has to be a
// list. Values are opendf texture ids, i.e. (archive << 7) | record.
const uint16_t sEmissiveFlats[] = {
  // archive 87 -- fireplace
    11136,
  // archive 101 -- lights (lit)
    12930, 12931, 12933, 12934, 12935, 12936, 12937, 12938,
    12939, 12940,
  // archive 190 -- lights
    24323, 24324, 24325,
  // archive 200 -- lights
    25607, 25608, 25609, 25610,
  // archive 202 -- statue
    25858,
  // archive 208 -- brewing potion
    26626,
  // archive 210 -- dungeon light fixtures
    26880, 26881, 26882, 26883, 26884, 26885, 26886, 26888,
    26889, 26891, 26893, 26894, 26895, 26896, 26897, 26898,
    26899, 26900, 26901, 26902, 26903, 26904, 26905, 26906,
    26907, 26908, 26909,
  // archive 253 -- dungeon misc flats
    32394, 32401, 32402, 32403, 32406, 32425, 32432, 32433,
    32434, 32435, 32436, 32459, 32461,
  // archive 273 -- ghost
    34944, 34945, 34946, 34947, 34948, 34949, 34950, 34951,
    34952, 34953, 34954, 34955, 34956, 34957, 34958,
  // archive 278 -- spectre
    35584, 35585, 35586, 35587, 35588, 35589, 35590, 35591,
    35592, 35593, 35594, 35595, 35596, 35597, 35598,
  // archive 280 -- fire daedra
    35840, 35841, 35842, 35843, 35844, 35845, 35846, 35847,
    35848, 35849, 35850, 35851, 35852, 35853, 35854, 35855,
    35856, 35857, 35858, 35859,
  // archive 281 -- daedra lord
    35968, 35969, 35970, 35971, 35972, 35973, 35974, 35975,
    35976, 35977, 35978, 35979, 35980, 35981, 35982, 35983,
    35984, 35985, 35986, 35987,
  // archive 290 -- flame atronach
    37120, 37121, 37122, 37123, 37124, 37125, 37126, 37127,
    37128, 37129, 37130, 37131, 37132, 37133, 37134, 37135,
    37136, 37137, 37138, 37139,
  // archive 356
    45568, 45570, 45571,
  // archive 375
    48000, 48001,
  // archive 376
    48128, 48129,
  // archive 377
    48256, 48257,
  // archive 378
    48384, 48385,
  // archive 379
    48512, 48513,
  // archive 380
    48643, 48645,
  // archive 400
    51202, 51203,
  // archive 405
    51842,
  // archive 434
    55555, 55557,
  // archive 473
    60544, 60545, 60546, 60547, 60548, 60549, 60550, 60551,
    60552, 60553, 60554, 60555, 60556, 60557, 60558,
};

bool isEmissiveFlat(size_t texid)
{
    // The table is binary-searched, so a mis-ordered entry would silently stop
    // matching rather than fail loudly.
    assert(std::is_sorted(std::begin(sEmissiveFlats), std::end(sEmissiveFlats)));
    return std::binary_search(std::begin(sEmissiveFlats), std::end(sEmissiveFlats),
                              static_cast<uint16_t>(texid));
}

} // namespace

osg::ref_ptr<osg::Node> MeshManager::loadFlat(size_t texid, bool centered, size_t *num_frames)
{
    auto iter = mFlatCache.find(std::make_pair(texid, centered));
    if(iter != mFlatCache.end())
    {
        osg::ref_ptr<osg::Node> node;
        if(iter->second.lock(node))
        {
            if(num_frames)
            {
                osg::ref_ptr<osg::Texture> tex = TextureManager::get().getTexture(texid);
                *num_frames = tex->getTextureDepth();
            }
            return node;
        }
    }

    if(!mFlatProgram)
    {
        mFlatProgram = new osg::Program();
        mFlatProgram->addShader(osgDB::readShaderFile(osg::Shader::VERTEX, "shaders/sprite.vert"));
        mFlatProgram->addShader(osgDB::readShaderFile(osg::Shader::FRAGMENT, "shaders/sprite.frag"));
    }

    int16_t xoffset, yoffset;
    float xscale, yscale;
    osg::ref_ptr<osg::Texture> tex = TextureManager::get().getTexture(
        texid, &xoffset, &yoffset, &xscale, &yscale
    );
    if(num_frames)
        *num_frames = tex->getTextureDepth();

    float width = tex->getTextureWidth();
    float height = tex->getTextureHeight();

    osg::Matrix mat(osg::Matrixf::scale(xscale, yscale, xscale));
    //mat.postMultTranslate(osg::Vec3(xoffset, yoffset, 0)); seems to be incorrect values??
    osg::ref_ptr<osg::MatrixTransform> base(new osg::MatrixTransform(mat));

    osg::ref_ptr<osg::Billboard> bb(new osg::Billboard());
    bb->setMode(osg::Billboard::AXIAL_ROT);
    bb->setAxis(osg::Vec3(0.0f, 1.0f, 0.0f));
    bb->setNormal(osg::Vec3(0.0f, 0.0f, -1.0f));

    osg::ref_ptr<osg::Vec3Array> vtxs(new osg::Vec3Array(4));
    (*vtxs)[0] = osg::Vec3(width* 0.5f, height*-0.5f, 0.0f);
    (*vtxs)[1] = osg::Vec3(width*-0.5f, height*-0.5f, 0.0f);
    (*vtxs)[2] = osg::Vec3(width*-0.5f, height* 0.5f, 0.0f);
    (*vtxs)[3] = osg::Vec3(width* 0.5f, height* 0.5f, 0.0f);
    osg::ref_ptr<osg::Vec2Array> texcrds(new osg::Vec2Array(4));
    (*texcrds)[0] = osg::Vec2(1.0f, 0.0f);
    (*texcrds)[1] = osg::Vec2(0.0f, 0.0f);
    (*texcrds)[2] = osg::Vec2(0.0f, 1.0f);
    (*texcrds)[3] = osg::Vec2(1.0f, 1.0f);
    osg::ref_ptr<osg::Vec3Array> nrms(new osg::Vec3Array(4));
    (*nrms)[0] = osg::Vec3(0.0f, 0.0f, -1.0f);
    (*nrms)[1] = osg::Vec3(0.0f, 0.0f, -1.0f);
    (*nrms)[2] = osg::Vec3(0.0f, 0.0f, -1.0f);
    (*nrms)[3] = osg::Vec3(0.0f, 0.0f, -1.0f);
    osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray(4));
    (*colors)[0] = osg::Vec4ub(255, 255, 255, 255);
    (*colors)[1] = osg::Vec4ub(255, 255, 255, 255);
    (*colors)[2] = osg::Vec4ub(255, 255, 255, 255);
    (*colors)[3] = osg::Vec4ub(255, 255, 255, 255);
    colors->setNormalize(true);

    osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject());
    vtxs->setVertexBufferObject(vbo);
    texcrds->setVertexBufferObject(vbo);
    nrms->setVertexBufferObject(vbo);
    colors->setVertexBufferObject(vbo);

    osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);
    geometry->setVertexArray(vtxs);
    geometry->setTexCoordArray(0, texcrds, osg::Array::BIND_PER_VERTEX);
    geometry->setNormalArray(nrms, osg::Array::BIND_PER_VERTEX);
    geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
    geometry->setUseDisplayList(false);
    geometry->setUseVertexBufferObjects(true);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 4));

    osg::StateSet *ss = geometry->getOrCreateStateSet();
    ss->setAttributeAndModes(mFlatProgram);
    // Alpha test is reversed, because the shader will set alpha=0 for texels
    // that should be kept, and consequently have no specular, and alpha=1 for
    // texels that should be dropped.
    ss->setAttributeAndModes(new osg::AlphaFunc(osg::AlphaFunc::LESS, 0.5f));
    ss->addUniform(new osg::Uniform("diffuseTex", 0));
    ss->setTextureAttribute(0, tex);
    // sprite.frag writes this straight into the diffuse light buffer (the main
    // pass's COLOR_BUFFER3), so a lit flat shows its own albedo at full
    // brightness instead of waiting on a light to reach it. Texels the alpha
    // test drops never get here, so the glow keeps the sprite's shape.
    if(isEmissiveFlat(texid))
        ss->addUniform(new osg::Uniform("illumination_color", osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f)));

    if(centered)
        bb->addDrawable(geometry);
    else
        bb->addDrawable(geometry, osg::Vec3(0.0f, height*-0.5f, 0.0f));
    base->addChild(bb);

    mFlatCache[std::make_pair(texid, centered)] = osg::ref_ptr<osg::Node>(base);
    return base;
}

osg::ref_ptr<osg::Node> MeshManager::getTerrain(int size)
{
    auto iter = mTerrainCache.find(size);
    if(iter != mTerrainCache.end())
    {
        osg::ref_ptr<osg::Node> node;
        if(iter->second.lock(node))
            return node;
    }

    if(!mTerrainProgram)
    {
        mTerrainProgram = new osg::Program();
        mTerrainProgram->addShader(osgDB::readShaderFile(osg::Shader::VERTEX, "shaders/terrain.vert"));
        mTerrainProgram->addShader(osgDB::readShaderFile(osg::Shader::FRAGMENT, "shaders/terrain.frag"));
    }

    osg::ref_ptr<osg::Vec3Array> vtxs(new osg::Vec3Array(4));
    (*vtxs)[0] = osg::Vec3(  0.0f, 0.0f,    0.0f);
    (*vtxs)[1] = osg::Vec3(  0.0f, 0.0f, -256.0f);
    (*vtxs)[2] = osg::Vec3(256.0f, 0.0f, -256.0f);
    (*vtxs)[3] = osg::Vec3(256.0f, 0.0f,    0.0f);
    osg::ref_ptr<osg::Vec2Array> texcrds(new osg::Vec2Array(4));
    (*texcrds)[0] = osg::Vec2(0.0f, 0.0f);
    (*texcrds)[1] = osg::Vec2(0.0f, 1.0f);
    (*texcrds)[2] = osg::Vec2(1.0f, 1.0f);
    (*texcrds)[3] = osg::Vec2(1.0f, 0.0f);

    osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject());
    vtxs->setVertexBufferObject(vbo);
    texcrds->setVertexBufferObject(vbo);

    osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);
    geometry->setVertexArray(vtxs);
    geometry->setTexCoordArray(0, texcrds, osg::Array::BIND_PER_VERTEX);
    geometry->setUseDisplayList(false);
    geometry->setUseVertexBufferObjects(true);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, vtxs->size(), size*size));
    geometry->setInitialBound(osg::BoundingBox(osg::Vec3(0.0f, -0.5f, -256.0f*size), osg::Vec3(256.0f*size, 0.5f, 0.0f)));

    osg::StateSet *ss = geometry->getOrCreateStateSet();
    ss->setAttributeAndModes(mTerrainProgram);
    ss->addUniform(new osg::Uniform("diffuseTex", 0));
    ss->addUniform(new osg::Uniform("tilemapTex", 1));

    osg::ref_ptr<osg::Geode> base(new osg::Geode());
    base->addDrawable(geometry);

    mTerrainCache[size] = osg::ref_ptr<osg::Node>(base);
    return base;
}

} // namespace Resource

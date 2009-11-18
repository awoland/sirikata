/*  cbr
 *  ObjectFactory.cpp
 *
 *  Copyright (c) 2009, Ewen Cheslack-Postava
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of cbr nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ObjectFactory.hpp"
#include "RandomMotionPath.hpp"
#include "Object.hpp"
#include "Random.hpp"

#include "QuakeMotionPath.hpp"
#include "StaticMotionPath.hpp"

#include "OSegTestMotionPath.hpp"


#include "Options.hpp"
#include "Statistics.hpp"

namespace CBR {

static UUID randomUUID() {
    uint8 raw_uuid[UUID::static_size];
    for(uint32 ui = 0; ui < UUID::static_size; ui++)
        raw_uuid[ui] = (uint8)rand() % 256;
    UUID id(raw_uuid, UUID::static_size);
    return id;
}

static UUID packUUID(const uint64 packid) {
    uint8 raw_uuid[UUID::static_size];
    const uint8* data_src = (const uint8*)&packid;
    for(uint32 ui = 0; ui < UUID::static_size; ui++)
        raw_uuid[ui] = data_src[ ui % sizeof(uint64) ];
    UUID id(raw_uuid, UUID::static_size);
    return id;
}

ObjectFactory::ObjectFactory(ObjectHostContext* ctx, const BoundingBox3f& region, const Duration& duration)
 : PollingService(ctx->mainStrand),
   mContext(ctx),
   mLocalIDSource(0)
{
    mProfiler = mContext->profiler->addStage("Object Factory Tick");
    // Note: we do random second in order make sure they get later connect times
    generatePackObjects(region, duration);
    generateRandomObjects(region, duration);
    setConnectTimes();
}

ObjectFactory::~ObjectFactory() {
    for(ObjectMap::iterator it = mObjects.begin(); it != mObjects.end();) {
        Object* obj = it->second;
        ++it;
        delete obj;
    }

    for(ObjectInputsMap::iterator it = mInputs.begin(); it != mInputs.end();) {
        ObjectInputs* inputs = it->second;
        ++it;
        delete inputs->motion;
        delete inputs;
    }
}

void ObjectFactory::generateRandomObjects(const BoundingBox3f& region, const Duration& duration) {
    Time start(Time::null());
    Time end = start + duration;
    Vector3f region_extents = region.extents();

    uint32 nobjects              = GetOption(OBJECT_NUM_RANDOM)->as<uint32>();
    if (nobjects == 0) return;
    bool simple                  =   GetOption(OBJECT_SIMPLE)->as<bool>();
    bool only_2d                 =       GetOption(OBJECT_2D)->as<bool>();
    float zfactor                =                  (only_2d ? 0.f : 1.f);
    std::string motion_path_type = GetOption(OBJECT_STATIC)->as<String>();
    float driftX                 = GetOption(OBJECT_DRIFT_X)->as<float>();
    float driftY                 = GetOption(OBJECT_DRIFT_Y)->as<float>();
    float driftZ                 = GetOption(OBJECT_DRIFT_Z)->as<float>();

    float percent_queriers       = GetOption(OBJECT_QUERY_FRAC)->as<float>();

    Vector3f driftVecDir(driftX,driftY,driftZ);


    for(uint32 i = 0; i < nobjects; i++) {
        UUID id = randomUUID();

        ObjectInputs* inputs = new ObjectInputs;

        Vector3f startpos = region.min() + Vector3f(randFloat()*region_extents.x, randFloat()*region_extents.y, randFloat()*region_extents.z * zfactor);

        float bounds_radius = (simple ? 10.f : (randFloat()*20));

        inputs->localID = mLocalIDSource++;

        if (motion_path_type == "static")//static
            inputs->motion = new StaticMotionPath(start, startpos);
        else if (motion_path_type == "drift") //drift
        {
          //   inputs->motion = new OSegTestMotionPath(start, end, startpos, 3, Duration::milliseconds((int64)1000), region, zfactor); // FIXME
          inputs->motion = new OSegTestMotionPath(start, end, startpos, 3, Duration::milliseconds((int64)1000), region, zfactor, driftVecDir); // FIXME
        }
        else //random
            inputs->motion = new RandomMotionPath(start, end, startpos, 3, Duration::milliseconds((int64)1000), region, zfactor); // FIXME
        inputs->bounds = BoundingSphere3f( Vector3f(0, 0, 0), bounds_radius );
        inputs->registerQuery = (randFloat() <= percent_queriers);
        inputs->queryAngle = SolidAngle(SolidAngle::Max / 900.f); // FIXME how to set this? variability by objects?
        inputs->connectAt = Duration::seconds(0.f);

        mObjectIDs.insert(id);
        mInputs[id] = inputs;
    }
}

/** Object packs are our own custom format for simple, fixed, object tests. The file format is
 *  a simple binary dump of each objects information:
 *
 *    struct ObjectInformation {
 *        uint64 object_id;
 *        double x;
 *        double y;
 *        double z;
 *        double radius;
 *    };
 *
 *  This gives the minimal information for a static object and allows you to seek directly to any
 *  object in the file, making it easy to split the file across multiple object hosts.
 */
void ObjectFactory::generatePackObjects(const BoundingBox3f& region, const Duration& duration) {
    Time start(Time::null());
    Time end = start + duration;
    Vector3f region_extents = region.extents();

    uint32 nobjects = GetOption(OBJECT_NUM_PACK)->as<uint32>();
    if (nobjects == 0) return;
    String pack_filename = GetOption(OBJECT_PACK)->as<String>();

    FILE* pack_file = fopen(pack_filename.c_str(), "rb");
    if (pack_file == NULL) {
        SILOG(objectfactory,error,"Couldn't open object pack file, not generating any objects.");
        return;
    }

    // First offset ourselves into the file
    uint32 pack_offset = GetOption(OBJECT_PACK_OFFSET)->as<uint32>();

    uint32 obj_pack_size =
        8 + // objid
        8 + // radius
        8 + // x
        8 + // y
        8 + // z
        0;
    int seek_success = fseek( pack_file, obj_pack_size * pack_offset, SEEK_SET );
    if (seek_success != 0) {
        SILOG(objectfactory,error,"Couldn't seek to appropriate offset in object pack file.");
        fclose(pack_file);
        return;
    }

    for(uint32 i = 0; i < nobjects; i++) {
        ObjectInputs* inputs = new ObjectInputs;

        uint64 pack_objid = 0;
        double x = 0, y = 0, z = 0, rad = 0;
        fread( &pack_objid, sizeof(uint64), 1, pack_file );
        fread( &x, sizeof(double), 1, pack_file );
        fread( &y, sizeof(double), 1, pack_file );
        fread( &z, sizeof(double), 1, pack_file );
        fread( &rad, sizeof(double), 1, pack_file );

        UUID id = packUUID(pack_objid);

        Vector3f startpos((float)x, (float)y, (float)z);
        float bounds_radius = (float)rad;

        inputs->localID = mLocalIDSource++;
        inputs->motion = new StaticMotionPath(start, startpos);
        inputs->bounds = BoundingSphere3f( Vector3f(0, 0, 0), bounds_radius );
        inputs->registerQuery = false;
        inputs->queryAngle = SolidAngle::Max;
        inputs->connectAt = Duration::seconds(0.f);

        mObjectIDs.insert(id);
        mInputs[id] = inputs;
    }

    fclose(pack_file);
}

void ObjectFactory::setConnectTimes() {
    Duration total_duration = GetOption(OBJECT_CONNECT_PHASE)->as<Duration>();

    for(ObjectInputsMap::iterator it = mInputs.begin(); it != mInputs.end(); it++) {
        ObjectInputs* inputs = it->second;
        inputs->connectAt = total_duration * ((float)inputs->localID/(float)mLocalIDSource);
    }
}

MotionPath* ObjectFactory::motion(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );
    assert( mInputs.find(id) != mInputs.end() );
    return mInputs[id]->motion;
}

BoundingSphere3f ObjectFactory::bounds(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );
    assert( mInputs.find(id) != mInputs.end() );
    return mInputs[id]->bounds;
}

bool ObjectFactory::registerQuery(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );
    assert( mInputs.find(id) != mInputs.end() );
    return mInputs[id]->registerQuery;
}

SolidAngle ObjectFactory::queryAngle(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );
    assert( mInputs.find(id) != mInputs.end() );
    return mInputs[id]->queryAngle;
}

bool ObjectFactory::isActive(const UUID& id) {
    ObjectMap::iterator it = mObjects.find(id);
    return (it != mObjects.end());
}

Object* ObjectFactory::object(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );

    ObjectMap::iterator it = mObjects.find(id);
    if (it != mObjects.end()) return it->second;

    Object* new_obj = new Object(this, id, motion(id), bounds(id), registerQuery(id), queryAngle(id), mContext);
    mObjects[id] = new_obj;
    return new_obj;
}

void ObjectFactory::poll() {
    mProfiler->started();

    Time t = mContext->time;
    for(ObjectIDSet::iterator it = mObjectIDs.begin(); it != mObjectIDs.end(); it++) {
        // Active objects receive a tick
        if (isActive(*it)) {
            object(*it)->tick();
            continue;
        }

        // Inactive objects are checked to see if they are ready to connect
        if (mInputs[*it]->connectAt <= (t - Time::null())) {
            Object* obj = object(*it);
            obj->connect();
        }
    }

    mProfiler->finished();
}

void ObjectFactory::notifyDestroyed(const UUID& id) {
    assert( mObjectIDs.find(id) != mObjectIDs.end() );
    assert( mObjects.find(id) != mObjects.end() );

    mObjects.erase(id);
}

} // namespace CBR

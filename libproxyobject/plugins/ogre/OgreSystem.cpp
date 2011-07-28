/*  Sirikata libproxyobject -- Ogre Graphics Plugin
 *  OgreSystem.cpp
 *
 *  Copyright (c) 2009, Daniel Reiter Horn
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
 *  * Neither the name of Sirikata nor the names of its contributors may
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
#include <sirikata/proxyobject/Platform.hpp>

#include <sirikata/core/util/Thread.hpp>

#include "OgreSystem.hpp"
#include "OgreSystemMouseHandler.hpp"
#include "OgrePlugin.hpp"
#include <sirikata/ogre/task/Event.hpp>
#include <sirikata/proxyobject/ProxyManager.hpp>
#include <sirikata/proxyobject/ProxyObject.hpp>
#include "ProxyEntity.hpp"
#include <Ogre.h>
#include "CubeMap.hpp"
#include <sirikata/ogre/input/InputDevice.hpp>
#include <sirikata/ogre/input/InputEvents.hpp>
#include "OgreMeshRaytrace.hpp"

#include <stdio.h>

using namespace std;

namespace Sirikata {
namespace Graphics {


OgreSystem::OgreSystem(Context* ctx)
 : OgreRenderer(ctx),
   mPrimaryCamera(NULL),
   mOverlayCamera(NULL),
   mOnReadyCallback(NULL),
   mOnResetReadyCallback(NULL)
{
    increfcount();
    mCubeMap=NULL;
    mInputManager=NULL;
    mOgreOwnedRenderWindow = false;
    mRenderTarget=NULL;
    mRenderWindow = NULL;
    mSceneManager=NULL;
    mMouseHandler=NULL;
    mRayQuery=NULL;
}


Time OgreSystem::simTime() {
    return mContext->simTime();
}

void OgreSystem::attachCamera(const String &renderTargetName, Camera* entity) {
    OgreRenderer::attachCamera(renderTargetName, entity);

    if (renderTargetName.empty()) {
        dlPlanner->setCamera(entity);
        std::vector<String> cubeMapNames;

        std::vector<Vector3f> cubeMapOffsets;
        std::vector<float> cubeMapNearPlanes;
        cubeMapNames.push_back("ExteriorCubeMap");
        cubeMapOffsets.push_back(Vector3f(0,0,0));
        cubeMapNearPlanes.push_back(10);
        cubeMapNames.push_back("InteriorCubeMap");
        cubeMapOffsets.push_back(Vector3f(0,0,0));
        cubeMapNearPlanes.push_back(0.1);
        try {
            mCubeMap=new CubeMap(this,cubeMapNames,512,cubeMapOffsets, cubeMapNearPlanes);
        }catch (std::bad_alloc&) {
            mCubeMap=NULL;
        }
    }
}

void OgreSystem::detachCamera(Camera* entity) {
    OgreRenderer::detachCamera(entity);

    if (mPrimaryCamera == entity) {
        mPrimaryCamera = NULL;
        delete mCubeMap;
        mCubeMap = NULL;
    }
}

void OgreSystem::instantiateAllObjects(ProxyManagerPtr pman)
{
    std::vector<SpaceObjectReference> allORefs;
    pman->getAllObjectReferences(allORefs);

    for (std::vector<SpaceObjectReference>::iterator iter = allORefs.begin(); iter != allORefs.end(); ++iter)
    {
        //instantiate each object in graphics system separately.
        ProxyObjectPtr toAdd = pman->getProxyObject(*iter);
        onCreateProxy(toAdd);
    }
}

namespace {
Transfer::DenseDataPtr read_file(const String& filename)
{
    ifstream myfile;
    myfile.open (filename.c_str());
    if (!myfile.is_open()) return Transfer::DenseDataPtr();

    myfile.seekg(0, ios::end);
    int length = myfile.tellg();
    myfile.seekg(0, ios::beg);

    Transfer::MutableDenseDataPtr output(new Transfer::DenseData(Transfer::Range(true)));
    output->setLength(length, true);

    // read data as a block:
    myfile.read((char*)output->writableData(), length);
    myfile.close();

    return output;
}
}

bool OgreSystem::initialize(VWObjectPtr viewer, const SpaceObjectReference& presenceid, const String& options) {
    if(!OgreRenderer::initialize(options)) return false;

    mViewer = viewer;
    mPresenceID = presenceid;

    ProxyManagerPtr proxyManager = mViewer->presence(presenceid);
    mViewer->addListener((SessionEventListener*)this);
    proxyManager->addListener(this);

    //initialize the Resource Download Planner
    dlPlanner = new SAngleDownloadPlanner(mContext);

    allocMouseHandler();

    // The default mesh is just loaded from a known local file
    using namespace boost::filesystem;
    String cube_path = (path(mResourcesDir) / "cube.dae").string();
    Transfer::DenseDataPtr cube_data = read_file(cube_path);
    mDefaultMesh = parseMeshWorkSync(Transfer::URI("file:///fake.dae"), Transfer::Fingerprint::null(), cube_data);

    //finish instantiation here
    instantiateAllObjects(proxyManager);

    mMouseHandler->mUIWidgetView->setReadyCallback( std::tr1::bind(&OgreSystem::handleUIReady, this) );
    mMouseHandler->mUIWidgetView->setResetReadyCallback( std::tr1::bind(&OgreSystem::handleUIResetReady, this) );
    mMouseHandler->mUIWidgetView->setUpdateViewportCallback( std::tr1::bind(&OgreSystem::handleUpdateUIViewport, this, _1, _2, _3, _4) );
    return true;
}

void OgreSystem::handleUIReady() {
    // Currently the only blocker for being ready is that the UI loaded. If we
    // end up with more, we may need to make this just set a flag and then check
    // if all conditions are met.
    if (mOnReadyCallback != NULL) mOnReadyCallback->invoke();
    mMouseHandler->uiReady();
}

void OgreSystem::handleUIResetReady() {
    // Currently the only blocker for being ready is that the UI loaded. If we
    // end up with more, we may need to make this just set a flag and then check
    // if all conditions are met.
    if (mOnResetReadyCallback != NULL) mOnResetReadyCallback->invoke();
    mMouseHandler->uiReady(); // Probably not really necessary since
                              // it's been called once already?
}

void OgreSystem::handleUpdateUIViewport(int32 left, int32 top, int32 right, int32 bottom) {
    if (mPrimaryCamera)
        mPrimaryCamera->setViewportDimensions(left, top, right, bottom);
}

void OgreSystem::windowResized(Ogre::RenderWindow *rw) {
    OgreRenderer::windowResized(rw);
    mMouseHandler->windowResized(rw->getWidth(), rw->getHeight());
}

bool OgreSystem::translateToDisplayViewport(float32 x, float32 y, float32* ox, float32* oy) {
    // x and y come in as [-1, 1], get it to [0,1] to match the normal viewport
    x = (x + 1.0f)/2.0f;
    y = (y + 1.0f)/2.0f;
    // Subtract out the offset to the subframe
    x -= mPrimaryCamera->getViewport()->getLeft();
    y -= mPrimaryCamera->getViewport()->getTop();
    // And scale to make the new [0-1] range fit in the correct sized
    // box
    x /= mPrimaryCamera->getViewport()->getWidth();
    y /= mPrimaryCamera->getViewport()->getHeight();
    // And convert back to [-1, 1] as we set the output values
    *ox = x * 2.f - 1.f;
    *oy = y * 2.f - 1.f;
    return !(*ox < -1.f || *ox > 1.f || *oy < -1.f || *ox > 1.f);
}

OgreSystem::~OgreSystem() {
    if (mViewer) {
        ProxyManagerPtr proxyManager = mViewer->presence(mPresenceID);
        proxyManager->removeListener(this);
    }

    decrefcount();
    destroyMouseHandler();
}

void OgreSystem::onCreateProxy(ProxyObjectPtr p)
{
    bool created = false;

    ProxyEntity* mesh = NULL;
    if (mEntityMap.find(p->getObjectReference()) != mEntityMap.end())
        mesh = mEntityMap[p->getObjectReference()];
    if (mesh == NULL)
        mesh = new ProxyEntity(this,p);
    mesh->initializeToProxy(p);
    mEntityMap[p->getObjectReference()] = mesh;
    dlPlanner->addNewObject(p,mesh);
    // Force validation. In the case of existing ProxyObjects, this
    // should trigger the download + display process
    mesh->validated();

    bool is_viewer = (p->getObjectReference() == mPresenceID);
    if (is_viewer)
    {
        if (mPrimaryCamera == NULL) {
            assert(mOverlayCamera == NULL);
            mOverlayCamera = new Camera(this, getOverlaySceneManager(), "overlay_camera");
            mOverlayCamera->attach("", 0, 0, Vector4f(0, 0, 0, 0), 1);

            Camera* cam = new Camera(this, getSceneManager(), String("Camera:") + mesh->id());
            cam->attach("", 0, 0, mBackgroundColor, 0);
            attachCamera("", cam);
            cam->setPosition(mesh->getOgrePosition());
            // Only store as primary camera now because doing it earlier loops back
            // to detachCamera, which then *removes* it as primary camera. It does
            // this because attach does a "normal" cleanup procedure before attaching.
            mPrimaryCamera = cam;
        }
    }
}

void OgreSystem::onDestroyProxy(ProxyObjectPtr p)
{
    dlPlanner->removeObject(p);
    // FIXME don't delete here because we want to mask proximity
    // additions/removals that aren't due to actual connect/disconnect.
    // See also ProxyEntity.cpp:destroy().
    //mEntityMap.erase(p->getObjectReference());
}


struct RayTraceResult {
    Ogre::Real mDistance;
    Ogre::MovableObject *mMovableObject;
    IntersectResult mResult;
    int mSubMesh;
    RayTraceResult() { mDistance=3.0e38f;mMovableObject=NULL;mSubMesh=-1;}
    RayTraceResult(Ogre::Real distance,
                   Ogre::MovableObject *moveableObject) {
        mDistance=distance;
        mMovableObject=moveableObject;
    }
    bool operator<(const RayTraceResult&other)const {
        if (mDistance==other.mDistance) {
            return mMovableObject<other.mMovableObject;
        }
        return mDistance<other.mDistance;
    }
    bool operator==(const RayTraceResult&other)const {
        return mDistance==other.mDistance&&mMovableObject==other.mMovableObject;
    }
};

Entity* OgreSystem::rayTrace(const Vector3d &position,
    const Vector3f &direction,
    int&resultCount,
    double &returnResult,
    Vector3f&returnNormal,
    int&subent,
    int which, SpaceObjectReference ignore) const{

    Ogre::Ray traceFrom(toOgre(position, getOffset()), toOgre(direction));
    return internalRayTrace(traceFrom,false,resultCount,returnResult,returnNormal, subent,NULL,false,which,ignore);
}

ProxyEntity* OgreSystem::getEntity(const SpaceObjectReference &proxyId) const {
    SceneEntitiesMap::const_iterator iter = mSceneEntities.find(proxyId.toString());
    if (iter != mSceneEntities.end()) {
        Entity* ent = (*iter).second;
        return static_cast<ProxyEntity*>(ent);
    } else {
        return NULL;
    }
}
ProxyEntity* OgreSystem::getEntity(const ProxyObjectPtr &proxy) const {
    return getEntity(proxy->getObjectReference());
}

bool OgreSystem::queryRay(const Vector3d&position,
                          const Vector3f&direction,
                          const double maxDistance,
                          ProxyObjectPtr ignore,
                          double &returnDistance,
                          Vector3f &returnNormal,
                          SpaceObjectReference &returnName) {
    int resultCount=0;
    int subent;
    Ogre::Ray traceFrom(toOgre(position, getOffset()), toOgre(direction));
    ProxyEntity * retval=internalRayTrace(traceFrom,false,resultCount,returnDistance,returnNormal,subent,NULL,false,0,mPresenceID);
    if (retval != NULL) {
        returnName= retval->getProxy().getObjectReference();
        return true;
    }
    return false;
}
ProxyEntity *OgreSystem::internalRayTrace(const Ogre::Ray &traceFrom, bool aabbOnly,int&resultCount,double &returnresult, Vector3f&returnNormal, int& returnSubMesh, IntersectResult *intersectResult, bool texcoord, int which, SpaceObjectReference ignore) const {
    Ogre::RaySceneQuery* mRayQuery;
    mRayQuery = mSceneManager->createRayQuery(Ogre::Ray());
    mRayQuery->setRay(traceFrom);
    mRayQuery->setSortByDistance(aabbOnly);
    mRayQuery->setQueryTypeMask(Ogre::SceneManager::WORLD_GEOMETRY_TYPE_MASK | Ogre::SceneManager::ENTITY_TYPE_MASK | Ogre::SceneManager::FX_TYPE_MASK);
    const Ogre::RaySceneQueryResult& resultList = mRayQuery->execute();

    ProxyEntity *toReturn = NULL;
    returnresult = 0;
    int count = 0;
    std::vector<RayTraceResult> fineGrainedResults;
    for (Ogre::RaySceneQueryResult::const_iterator iter  = resultList.begin();
         iter != resultList.end(); ++iter) {
        const Ogre::RaySceneQueryResultEntry &result = (*iter);

        Ogre::BillboardSet* foundBillboard = dynamic_cast<Ogre::BillboardSet*>(result.movable);
        Ogre::Entity *foundEntity = dynamic_cast<Ogre::Entity*>(result.movable);

        if (foundEntity != NULL) {
            ProxyEntity *ourEntity = ProxyEntity::fromMovableObject(result.movable);
            if (!ourEntity) continue;
            if (ourEntity->id() == ignore.toString()) continue;
        }

        RayTraceResult rtr(result.distance,result.movable);
        bool passed=aabbOnly&&result.distance > 0;
        if (aabbOnly==false) {
            if (foundEntity != NULL) {
                rtr.mDistance=3.0e38f;
                ProxyEntity *ourEntity = ProxyEntity::fromMovableObject(result.movable);
                Ogre::Ray meshRay = OgreMesh::transformRay(ourEntity->getSceneNode(), traceFrom);
                Ogre::Mesh *mesh = foundEntity->getMesh().get();
                uint16 numSubMeshes = mesh->getNumSubMeshes();
                std::vector<TriVertex> sharedVertices;
                for (uint16 ndx = 0; ndx < numSubMeshes; ndx++) {
                    Ogre::SubMesh *submesh = mesh->getSubMesh(ndx);
                    OgreMesh ogreMesh(submesh, texcoord, sharedVertices);
                    IntersectResult intRes;
                    ogreMesh.intersect(ourEntity->getSceneNode(), meshRay, intRes);
                    if (intRes.intersected && intRes.distance < rtr.mDistance && intRes.distance > 0 ) {
                        rtr.mResult = intRes;
                        rtr.mMovableObject = result.movable;
                        rtr.mDistance=intRes.distance;
                        rtr.mSubMesh=ndx;
                        passed=true;
                    }
                }
            }
            else if (foundBillboard != NULL) {
                // FIXME real check against the object if checking bounding box
                // isn't sufficient (but maybe it is for billboards since ogre
                // forces them to face the user? but they also have an
                // orientation? but the billboard set seems to let you control
                // orientation? I don't know...)
                // FIXME fake distances
                float32 dist = (foundBillboard->getWorldBoundingSphere().getCenter() - traceFrom.getOrigin()).length();
                IntersectResult intRes;
                intRes.intersected = true;
                intRes.distance = dist;
                intRes.normal = fromOgre(-traceFrom.getDirection().normalisedCopy());
                // No triangle or uv...
                rtr.mResult = intRes;
                rtr.mMovableObject = foundBillboard;
                rtr.mDistance = dist;
                rtr.mSubMesh = -1;
                passed = true;
            }
        }
        if (passed) {
            fineGrainedResults.push_back(rtr);
            ++count;
        }
    }
    if (!aabbOnly) {
        std::sort(fineGrainedResults.begin(),fineGrainedResults.end());
    }
    if (count > 0) {
        which %= count;
        if (which < 0) {
            which += count;
        }
        for (std::vector<RayTraceResult>::const_iterator iter  = fineGrainedResults.begin()+which,iterEnd=fineGrainedResults.end();
             iter != iterEnd; ++iter) {
            const RayTraceResult &result = (*iter);
            ProxyEntity *foundEntity = ProxyEntity::fromMovableObject(result.mMovableObject);
            if (foundEntity) {
                toReturn = foundEntity;
                returnresult = result.mDistance;
                returnNormal=result.mResult.normal;
                returnSubMesh=result.mSubMesh;
                if(intersectResult)*intersectResult=result.mResult;
                break;
            }
        }
    }
    mRayQuery->clearResults();
    if (mRayQuery) {
        mSceneManager->destroyQuery(mRayQuery);
    }
    resultCount=count;
    return toReturn;
}

void OgreSystem::poll(){
    OgreRenderer::poll();
    Task::LocalTime curFrameTime(Task::LocalTime::now());
    tickInputHandler(curFrameTime);
}

bool OgreSystem::renderOneFrame(Task::LocalTime t, Duration frameTime) {
    bool cont = OgreRenderer::renderOneFrame(t, frameTime);

    if(WebViewManager::getSingletonPtr())
    {
        // HACK: WebViewManager is static, but points to a RenderTarget! If OgreRenderer dies, we will crash.
        static bool webViewInitialized = false;
        if(!webViewInitialized) {
            if (mOverlayCamera) {
                WebViewManager::getSingleton().setDefaultViewport(mOverlayCamera->getViewport());
                webViewInitialized = true;
            }
            // else, keep waiting for a camera to appear (may require connecting to a space).
        }
        if (webViewInitialized) {
            WebViewManager::getSingleton().Update();
        }
    }

    return cont;
}
void OgreSystem::preFrame(Task::LocalTime currentTime, Duration frameTime) {
    OgreRenderer::preFrame(currentTime, frameTime);
}

void OgreSystem::postFrame(Task::LocalTime current, Duration frameTime) {
    OgreRenderer::postFrame(current, frameTime);
    Ogre::FrameEvent evt;
    evt.timeSinceLastEvent=frameTime.toMicroseconds()*1000000.;
    evt.timeSinceLastFrame=frameTime.toMicroseconds()*1000000.;
    if (mCubeMap) {
        mCubeMap->frameEnded(evt);
    }
}


// ConnectionEventListener Interface
void OgreSystem::onConnected(const Network::Address& addr)
{
}

void OgreSystem::onDisconnected(const Network::Address& addr, bool requested, const String& reason) {
    if (!requested) {
        SILOG(ogre,fatal,"Got disconnected from space server: " << reason);
        quit(); // FIXME
    }
    else
        SILOG(ogre,warn,"Disconnected from space server.");
}

void OgreSystem::onDisconnected(SessionEventProviderPtr from, const SpaceObjectReference& name) {
    mViewer->removeListener((SessionEventListener*)this);
    SILOG(ogre,info,"Got disconnected from space server.");
    mMouseHandler->alert("Disconnected", "Lost connection to space server...");
}


void OgreSystem::allocMouseHandler() {
    mMouseHandler = new OgreSystemMouseHandler(this);
    mMouseHandler->ensureUI();
}
void OgreSystem::destroyMouseHandler() {
    if (mMouseHandler) {
        delete mMouseHandler;
    }
}

void OgreSystem::tickInputHandler(const Task::LocalTime& t) const {
    if (mMouseHandler != NULL)
        mMouseHandler->tick(t);
}

boost::any OgreSystem::invoke(vector<boost::any>& params)
{
    // Decode the command. First argument is the "function name"
    if (params.empty() || !Invokable::anyIsString(params[0]))
        return boost::any();

    string name = Invokable::anyAsString(params[0]);
    SILOG(ogre,detailed,"Invoking the function " << name);

    if(name == "onReady")
        return setOnReady(params);
    else if(name == "evalInUI")
        return evalInUI(params);
    else if(name == "createWindow")
        return createWindow(params);
    else if(name == "createWindowFile")
        return createWindowFile(params);
    else if(name == "addModuleToUI")
        return addModuleToUI(params);
    else if(name == "addTextModuleToUI")
        return addTextModuleToUI(params);
    else if(name == "createWindowHTML")
        return createWindowHTML(params);
    else if(name == "setInputHandler")
        return setInputHandler(params);
    else if(name == "quit")
        quit();
    else if (name == "suspend")
        suspend();
    else if (name == "toggleSuspend")
        toggleSuspend();
    else if (name == "resume")
        resume();
    else if (name == "screenshot")
        screenshot("screenshot.png");
    else if (name == "pick")
        return pick(params);
    else if (name == "bbox")
        return bbox(params);
    else if (name == "visible")
        return visible(params);
    else if (name == "camera")
        return getCamera(params);
    else if (name == "setCameraPosition")
        return setCameraPosition(params);
    else if (name == "setCameraOrientation")
        return setCameraOrientation(params);
    else if (name == "getAnimationList")
        return getAnimationList(params);
    else if (name == "startAnimation")
        return startAnimation(params);
    else if (name == "stopAnimation")
        return stopAnimation(params);
    else
        return OgreRenderer::invoke(params);

    return boost::any();
}

boost::any OgreSystem::setOnReady(std::vector<boost::any>& params) {
    if (params.size() < 2) return boost::any();
    // On ready cb
    if (!Invokable::anyIsInvokable(params[1])) return boost::any();
    // On reset ready cb
    if (params.size() > 2 && !Invokable::anyIsInvokable(params[2])) return boost::any();

    Invokable* handler = Invokable::anyAsInvokable(params[1]);
    mOnReadyCallback = handler;

    Invokable* reset_handler = Invokable::anyAsInvokable(params[2]);
    mOnResetReadyCallback = reset_handler;

    return boost::any();
}

boost::any OgreSystem::evalInUI(std::vector<boost::any>& params) {
    if (params.size() < 2) return boost::any();
    if (!Invokable::anyIsString(params[1])) return boost::any();

    mMouseHandler->mUIWidgetView->evaluateJS(Invokable::anyAsString(params[1]));

    return boost::any();
}

boost::any OgreSystem::createWindow(const String& window_name, bool is_html, bool is_file, String content, uint32 width, uint32 height) {
    WebViewManager* wvManager = WebViewManager::getSingletonPtr();
    WebView* ui_wv = wvManager->getWebView(window_name);
    if(!ui_wv)
    {
        ui_wv = wvManager->createWebView(window_name, window_name, width, height, OverlayPosition(RP_TOPLEFT));
        if (is_html)
            ui_wv->loadHTML(content);
        else if (is_file)
            ui_wv->loadFile(content);
        else
            ui_wv->loadURL(content);
    }
    Invokable* inn = ui_wv;
    return Invokable::asAny(inn);
}

boost::any OgreSystem::createWindow(vector<boost::any>& params) {
    // Create a window using the specified url
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsString(params[1]) || !Invokable::anyIsString(params[2])) return boost::any();

    String window_name = Invokable::anyAsString(params[1]);
    String html_url = Invokable::anyAsString(params[2]);
    uint32 width = (params.size() > 3 && Invokable::anyIsNumeric(params[3])) ? Invokable::anyAsNumeric(params[3]) : 300;
    uint32 height = (params.size() > 4 && Invokable::anyIsNumeric(params[4])) ? Invokable::anyAsNumeric(params[4]) : 300;
    return createWindow(window_name, false, false, html_url, width, height);
}

boost::any OgreSystem::createWindowFile(vector<boost::any>& params) {
    // Create a window using the specified url
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsString(params[1]) || !Invokable::anyIsString(params[2])) return boost::any();

    String window_name = Invokable::anyAsString(params[1]);
    String html_url = Invokable::anyAsString(params[2]);
    uint32 width = (params.size() > 3 && Invokable::anyIsNumeric(params[3])) ? Invokable::anyAsNumeric(params[3]) : 300;
    uint32 height = (params.size() > 4 && Invokable::anyIsNumeric(params[4])) ? Invokable::anyAsNumeric(params[4]) : 300;

    return createWindow(window_name, false, true, html_url, width, height);
}

boost::any OgreSystem::addModuleToUI(std::vector<boost::any>& params) {
    if (params.size() != 3) return boost::any();
    if (!anyIsString(params[1]) || !anyIsString(params[2])) return boost::any();

    String window_name = anyAsString(params[1]);
    String html_url = anyAsString(params[2]);

    if (!mMouseHandler) return boost::any();

    // Note the ../, this is because that loadModule executes from within data/chrome
    mMouseHandler->mUIWidgetView->evaluateJS("loadModule('../" + html_url + "')");
    Invokable* inn = mMouseHandler->mUIWidgetView;
    return Invokable::asAny(inn);
}

boost::any OgreSystem::addTextModuleToUI(std::vector<boost::any>& params) {
    if (params.size() != 3) return boost::any();
    if (!anyIsString(params[1]) || !anyIsString(params[2])) return boost::any();

    String window_name = anyAsString(params[1]);
    String module_js = anyAsString(params[2]);

    if (!mMouseHandler) return boost::any();

    // Note that we assume escaped js
    mMouseHandler->mUIWidgetView->evaluateJS("loadModuleText(" + module_js + ")");
    Invokable* inn = mMouseHandler->mUIWidgetView;
    return Invokable::asAny(inn);
}

boost::any OgreSystem::createWindowHTML(vector<boost::any>& params) {
    // Create a window using the specified HTML content
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsString(params[1]) || !Invokable::anyIsString(params[2])) return boost::any();

    String window_name = Invokable::anyAsString(params[1]);
    String html_script = Invokable::anyAsString(params[2]);
    uint32 width = (params.size() > 3 && Invokable::anyIsNumeric(params[3])) ? Invokable::anyAsNumeric(params[3]) : 300;
    uint32 height = (params.size() > 4 && Invokable::anyIsNumeric(params[4])) ? Invokable::anyAsNumeric(params[4]) : 300;

    return createWindow(window_name, true, false, html_script, width, height);
}

boost::any OgreSystem::setInputHandler(vector<boost::any>& params) {
    if (params.size() < 2) return boost::any();
    if (!Invokable::anyIsInvokable(params[1])) return boost::any();

    Invokable* handler = Invokable::anyAsInvokable(params[1]);
    mMouseHandler->setDelegate(handler);
    return boost::any();
}

boost::any OgreSystem::pick(vector<boost::any>& params) {
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsNumeric(params[1])) return boost::any();
    if (!Invokable::anyIsNumeric(params[2])) return boost::any();

    float x = Invokable::anyAsNumeric(params[1]);
    float y = Invokable::anyAsNumeric(params[2]);
    // We support bool ignore_self or SpaceObjectReference as third
    SpaceObjectReference ignore = SpaceObjectReference::null();
    if (params.size() > 3) {
        if (Invokable::anyIsBoolean(params[3]) && Invokable::anyAsBoolean(params[3]))
            ignore = mPresenceID;
        else if (Invokable::anyIsObject(params[3]))
            ignore = Invokable::anyAsObject(params[3]);
    }
    Vector3f hitPoint;
    SpaceObjectReference result = mMouseHandler->pick(Vector2f(x,y), 1, ignore, &hitPoint);

    Invokable::Dict pick_result;
    pick_result["object"] = Invokable::asAny(result);
    Invokable::Dict pick_position;
    pick_position["x"] = Invokable::asAny(hitPoint.x);
    pick_position["y"] = Invokable::asAny(hitPoint.y);
    pick_position["z"] = Invokable::asAny(hitPoint.z);
    pick_result["position"] = Invokable::asAny(pick_position);

    return Invokable::asAny(pick_result);
}

boost::any OgreSystem::getAnimationList(vector<boost::any>& params) {
    if (params.size() < 2) return boost::any();
    if (!Invokable::anyIsObject(params[1])) return boost::any();

    SpaceObjectReference objid = Invokable::anyAsObject(params[1]);

    if (mSceneEntities.find(objid.toString()) == mSceneEntities.end()) return boost::any();
    Entity* ent = mSceneEntities.find(objid.toString())->second;

    const std::vector<String> animationList = ent->getAnimationList();
    Invokable::Array arr;

    for (uint32 i = 0; i < animationList.size(); i++) {
      arr.push_back(Invokable::asAny(animationList[i]));
    }

    return arr;
}

boost::any OgreSystem::startAnimation(std::vector<boost::any>& params) {
  if (params.size() < 3) return boost::any();
  if (!Invokable::anyIsObject(params[1])) return boost::any();
  if ( !anyIsString(params[2]) ) return boost::any();

  SpaceObjectReference objid = Invokable::anyAsObject(params[1]);
  String animation_name = Invokable::anyAsString(params[2]);

  if (mSceneEntities.find(objid.toString()) == mSceneEntities.end()) return boost::any();

  Entity* ent = mSceneEntities.find(objid.toString())->second;
  ent->setAnimation(animation_name);

  return boost::any();
}

boost::any OgreSystem::stopAnimation(std::vector<boost::any>& params) {
  if (params.size() < 2) return boost::any();
  if (!Invokable::anyIsObject(params[1])) return boost::any();  

  SpaceObjectReference objid = Invokable::anyAsObject(params[1]);  

  if (mSceneEntities.find(objid.toString()) == mSceneEntities.end()) return boost::any();

  Entity* ent = mSceneEntities.find(objid.toString())->second;
  ent->setAnimation("");

  return boost::any();
}

boost::any OgreSystem::bbox(vector<boost::any>& params) {
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsObject(params[1])) return boost::any();
    if (!Invokable::anyIsBoolean(params[2])) return boost::any();

    SpaceObjectReference objid = Invokable::anyAsObject(params[1]);
    bool setting = Invokable::anyAsBoolean(params[2]);

    if (mSceneEntities.find(objid.toString()) == mSceneEntities.end()) return boost::any();
    Entity* ent = mSceneEntities.find(objid.toString())->second;
    ent->setSelected(setting);

    return boost::any();
}

boost::any OgreSystem::visible(vector<boost::any>& params) {
    if (params.size() < 3) return boost::any();
    if (!Invokable::anyIsObject(params[1])) return boost::any();
    if (!Invokable::anyIsBoolean(params[2])) return boost::any();

    SpaceObjectReference objid = Invokable::anyAsObject(params[1]);
    bool setting = Invokable::anyAsBoolean(params[2]);

    if (mSceneEntities.find(objid.toString()) == mSceneEntities.end()) return boost::any();
    Entity* ent = mSceneEntities.find(objid.toString())->second;
    ent->setVisible(setting);

    return boost::any();
}

boost::any OgreSystem::getCamera(vector<boost::any>& params) {
    if (mPrimaryCamera == NULL) return boost::any();

    Ogre::Camera* cam = mPrimaryCamera->getOgreCamera();

    // Just returns a "struct" with basic camera info
    Invokable::Dict camera_info;

    float32 aspect = cam->getAspectRatio();
    camera_info["aspectRatio"] = Invokable::asAny(aspect);

    float32 fovy = cam->getFOVy().valueRadians();
    float32 fovx = fovy * aspect;
    Invokable::Dict camera_fov;
    camera_fov["x"] = Invokable::asAny(fovx);
    camera_fov["y"] = Invokable::asAny(fovy);
    camera_info["fov"] = Invokable::asAny(camera_fov);

    Invokable::Dict camera_pos;
    Vector3d pos = mPrimaryCamera->getPosition();
    camera_pos["x"] = Invokable::asAny(pos.x);
    camera_pos["y"] = Invokable::asAny(pos.y);
    camera_pos["z"] = Invokable::asAny(pos.z);
    camera_info["position"] = Invokable::asAny(camera_pos);

    Invokable::Dict camera_orient;
    Quaternion orient = mPrimaryCamera->getOrientation();
    camera_orient["x"] = Invokable::asAny(orient.x);
    camera_orient["y"] = Invokable::asAny(orient.y);
    camera_orient["z"] = Invokable::asAny(orient.z);
    camera_orient["w"] = Invokable::asAny(orient.w);
    camera_info["orientation"] = Invokable::asAny(camera_orient);

    return Invokable::asAny(camera_info);
}

boost::any OgreSystem::setCameraPosition(vector<boost::any>& params) {
    if (mPrimaryCamera == NULL) return boost::any();
    if (params.size() < 4) return boost::any();
    if (!Invokable::anyIsNumeric(params[1]) || !Invokable::anyIsNumeric(params[2]) || !Invokable::anyIsNumeric(params[3])) return boost::any();

    double x = Invokable::anyAsNumeric(params[1]),
        y = Invokable::anyAsNumeric(params[2]),
        z = Invokable::anyAsNumeric(params[3]);

    mPrimaryCamera->setPosition(Vector3d(x, y, z));

    return boost::any();
}

boost::any OgreSystem::setCameraOrientation(vector<boost::any>& params) {
    if (mPrimaryCamera == NULL) return boost::any();
    if (params.size() < 5) return boost::any();
    if (!Invokable::anyIsNumeric(params[1]) || !Invokable::anyIsNumeric(params[2]) || !Invokable::anyIsNumeric(params[3]) || !Invokable::anyIsNumeric(params[4])) return boost::any();

    double x = Invokable::anyAsNumeric(params[1]),
        y = Invokable::anyAsNumeric(params[2]),
        z = Invokable::anyAsNumeric(params[3]),
        w = Invokable::anyAsNumeric(params[4]);

    mPrimaryCamera->setOrientation(Quaternion(x, y, z, w, Quaternion::XYZW()));

    return boost::any();
}

}
}

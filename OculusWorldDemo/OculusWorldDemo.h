#ifndef OCULUSWORLDDEMO_H_
#define OCULUSWORLDDEMO_H_
#include "OVR.h"

#include "Player.h"
#include "../CommonSrc/Platform/Platform_Default.h"
#include "../CommonSrc/Render/Render_Device.h"
#include "../CommonSrc/Render/Render_XMLSceneLoader.h"
#include "../CommonSrc/Render/Render_FontEmbed_DejaVu48.h"

#include <Kernel/OVR_SysFile.h>

// Filename to be loaded by default, searching specified paths.
#define WORLDDEMO_ASSET_FILE  "Tuscany.xml"
#define WORLDDEMO_ASSET_PATH1 "Assets/Tuscany/"
#define WORLDDEMO_ASSET_PATH2 "../Assets/Tuscany/"
// This path allows the shortcut to work.
#define WORLDDEMO_ASSET_PATH3 "Samples/OculusWorldDemo/Assets/Tuscany/"


using namespace OVR;
using namespace OVR::Platform;
using namespace OVR::Render;

//-------------------------------------------------------------------------------------
// ***** OculusWorldDemo Description

// This app renders a simple flat-shaded room allowing the user to move along the
// floor and look around with an HMD, mouse and keyboard. The following keys work:
//
//  'W', 'S', 'A', 'D' and Arrow Keys - Move forward, back; strafe left/right.
//  F1 - No stereo, no distortion.
//  F2 - Stereo, no distortion.
//  F3 - Stereo and distortion.
//  F8 - Toggle MSAA.
//  F9 - Set FullScreen mode on the HMD; necessary for previewing content with Rift.
//
// Important Oculus-specific logic can be found at following locations:
//
//  OculusWorldDemoApp::OnStartup - This function will initialize OVR::DeviceManager and HMD,
//									creating SensorDevice and attaching it to SensorFusion.
//									This needs to be done before obtaining sensor data.
//
//  OculusWorldDemoApp::OnIdle    - Here we poll SensorFusion for orientation, apply it
//									to the scene and handle movement.
//									Stereo rendering is also done here, by delegating to
//									to Render function for each eye.
//

//-------------------------------------------------------------------------------------
// ***** OculusWorldDemo Application class

// An instance of this class is created on application startup (main/WinMain).
// It then works as follows:
//  - Graphics and HMD setup is done OculusWorldDemoApp::OnStartup(). This function
//    also creates the room model from Slab declarations.
//  - Per-frame processing is done in OnIdle(). This function processes
//    sensor and movement input and then renders the frame.
//  - Additional input processing is done in OnMouse, OnKey and OnGamepad.

class OculusWorldDemoApp : public Application, public MessageHandler
{
public:
    OculusWorldDemoApp();
    ~OculusWorldDemoApp();

    virtual int  OnStartup(int argc, const char** argv);
    virtual void OnIdle();

    virtual void OnMouseMove(int x, int y, int modifiers);
    virtual void OnKey(KeyCode key, int chr, bool down, int modifiers);
    virtual void OnGamepad(const GamepadState& pad);
    virtual void OnResize(int width, int height);

    virtual void OnMessage(const Message& msg);

    void         Render(const StereoEyeParams& stereo);

    // Sets temporarily displayed message for adjustments
    void         SetAdjustMessage(const char* format, ...);
    // Overrides current timeout, in seconds (not the future default value);
    // intended to be called right after SetAdjustMessage.
    void         SetAdjustMessageTimeout(float timeout);

    // Stereo setting adjustment functions.
    // Called with deltaTime when relevant key is held.
    void         AdjustFov(float dt);
    void         AdjustAspect(float dt);
    void         AdjustIPD(float dt);
    void         AdjustEyeHeight(float dt);

    void         AdjustDistortion(float dt, int kIndex, const char* label);
    void         AdjustDistortionK0(float dt)  { AdjustDistortion(dt, 0, "K0"); }
    void         AdjustDistortionK1(float dt)  { AdjustDistortion(dt, 1, "K1"); }
    void         AdjustDistortionK2(float dt)  { AdjustDistortion(dt, 2, "K2"); }
    void         AdjustDistortionK3(float dt)  { AdjustDistortion(dt, 3, "K3"); }

    // Adds room model to scene.
    void         PopulateScene(const char* fileName);
    void         PopulatePreloadScene();
    void		 ClearScene();

protected:
    RenderDevice*       pRender;
    RendererParams      RenderParams;
    int                 Width, Height;
    int                 Screen;
    int                 FirstScreenInCycle;

    // *** Oculus HMD Variables
    Ptr<DeviceManager>  pManager;
    Ptr<SensorDevice>   pSensor;
    Ptr<HMDDevice>      pHMD;
    SensorFusion        SFusion;
    HMDInfo             HMDInfo;

    Ptr<LatencyTestDevice>  pLatencyTester;
    Util::LatencyTest   LatencyUtil;

    double              LastUpdate;
    int                 FPS;
    int                 FrameCounter;
    double              NextFPSUpdate;

    Array<Ptr<CollisionModel> > CollisionModels;
    Array<Ptr<CollisionModel> > GroundCollisionModels;

    // Loading process displays screenshot in first frame
    // and then proceeds to load until finished.
    enum LoadingStateType
    {
        LoadingState_Frame0,
        LoadingState_DoLoad,
        LoadingState_Finished
    };

	// Player
	Player				Player;
    Matrix4f            View;
    Scene               MainScene;
    Scene               LoadingScene;
    Scene               GridScene;
    LoadingStateType    LoadingState;

    Ptr<ShaderFill>     LitSolid, LitTextures[4];

    // Stereo view parameters.
    StereoConfig        SConfig;
    PostProcessType     PostProcess;

    // LOD
    String	            MainFilePath;
    Array<String>       LODFilePaths;
    int					ConsecutiveLowFPSFrames;
    int					CurrentLODFileIndex;

    float               DistortionK0;
    float               DistortionK1;
    float               DistortionK2;
    float               DistortionK3;

    String              AdjustMessage;
    double              AdjustMessageTimeout;

    // Saved distortion state.
    float               SavedK0, SavedK1, SavedK2, SavedK3;
    float               SavedESD, SavedAspect, SavedEyeDistance;

    // Allows toggling color around distortion.
    Color               DistortionClearColor;

    // Stereo settings adjustment state.
    typedef void (OculusWorldDemoApp::*AdjustFuncType)(float);
    bool                ShiftDown;
    AdjustFuncType      pAdjustFunc;
    float               AdjustDirection;

    enum SceneRenderMode
    {
        Scene_World,
        Scene_Grid,
        Scene_Both,

    };
    SceneRenderMode    SceneMode;


    enum TextScreen
    {
        Text_None,
        Text_Orientation,
        Text_Config,
        Text_Help,
        Text_Count
    };
    TextScreen          TextScreen;

    Model* CreateModel(Vector3f pos, struct SlabModel* sm);
    Model* CreateBoundingModel(CollisionModel &cm);
    void PopulateLODFileNames();
    void DropLOD();
    void RaiseLOD();
    void CycleDisplay();
};
#endif
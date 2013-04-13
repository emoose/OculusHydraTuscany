/************************************************************************************

Filename    :   OculusWorldDemo.cpp
Content     :   First-person view test application for Oculus Rift
Created     :   October 4, 2012
Authors     :   Michael Antonov, Andrew Reisse
				Peter Hoff, Dan Goodman, Bryan Croteau

Copyright   :   Copyright 2012 Oculus VR, Inc. All Rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*************************************************************************************/

#include "OculusWorldDemo.h"

#define HYDRA_INIT_WAIT 5
#define HYDRA_DEAD_ZONE 5

const char * HydraTextString = 0;
bool HydraSetupFinished = false;

float HydraControlRotation[4];
float BaseHydraRotation[4];

// This is the callback that gets registered with the sixenseUtils::controller_manager. It will get called each time the user completes
// one of the setup steps so that the game can update the instructions to the user. If the engine supports texture mapping, the 
// controller_manager can prove a pathname to a image file that contains the instructions in graphic form.
// The controller_manager serves the following functions:
//  1) Makes sure the appropriate number of controllers are connected to the system. The number of required controllers is designaged by the
//     game type (ie two player two controller game requires 4 controllers, one player one controller game requires one)
//  2) Makes the player designate which controllers are held in which hand.
//  3) Enables hemisphere tracking by calling the Sixense API call sixenseAutoEnableHemisphereTracking. After this is completed full 360 degree
//     tracking is possible.
void SixenseCallBack( sixenseUtils::ControllerManager::setup_step step )
{
	if( sixenseUtils::getTheControllerManager()->isMenuVisible() )
	{
		HydraSetupFinished = false;
		HydraTextString = sixenseUtils::getTheControllerManager()->getStepString();
		HydraTextString = HydraTextString;
	}
	else
		HydraSetupFinished = true;
}

//-------------------------------------------------------------------------------------

OculusWorldDemoApp::OculusWorldDemoApp()
    : pRender(0),
      LastUpdate(0),
      LoadingState(LoadingState_Frame0),
      // Initial location
      SConfig(),
      PostProcess(PostProcess_Distortion),
      DistortionClearColor(0, 0, 0),

      ShiftDown(false),
      pAdjustFunc(0),
      AdjustDirection(1.0f),
      SceneMode(Scene_World),
      TextScreen(Text_None)
{
    Width  = 1280;
    Height = 800;
    Screen = 0;
    FirstScreenInCycle = 0;

    FPS = 0;
    FrameCounter = 0;
    NextFPSUpdate = 0;

    ConsecutiveLowFPSFrames = 0;
    CurrentLODFileIndex = 0;

    AdjustMessageTimeout = 0;
	
	HydraEnabled = false;
	FoundHydra = false;
	Button1Pressed = false;
	HydraLeftPos = Vector3f(0, 0, 0);
	BaseHydraLeftPos = Vector3f(0, 0, 0);
}

OculusWorldDemoApp::~OculusWorldDemoApp()
{
    RemoveHandlerFromDevices();
    if(DejaVu.fill)
    {
        DejaVu.fill->Release();
    }
    pSensor.Clear();
    pHMD.Clear();
	CollisionModels.ClearAndRelease();
	GroundCollisionModels.ClearAndRelease();
	sixenseExit();
}

int OculusWorldDemoApp::OnStartup(int argc, const char** argv)
{
	// *** Razer Hydra init

	int base = 0;
	sixenseAllControllerData acd;

	int ret = sixenseInit();

	// Init the controller manager. This makes sure the controllers are present, assigned to left and right hands, and that
	// the hemisphere calibration is complete.
	sixenseUtils::getTheControllerManager()->setGameType( sixenseUtils::ControllerManager::ONE_PLAYER_TWO_CONTROLLER );
	sixenseUtils::getTheControllerManager()->registerSetupCallback( SixenseCallBack );

	sixenseSetActiveBase(0);
	sixenseGetAllNewestData( &acd );
	sixenseUtils::getTheControllerManager()->update( &acd );

	bool found = false;
	double time = pPlatform->GetAppTime();
	double time2 = time;
	while((time2 = pPlatform->GetAppTime()) - time < HYDRA_INIT_WAIT && !FoundHydra)
		for( base=0; base<sixenseGetMaxBases(); base++ )
			if(FoundHydra = sixenseIsBaseConnected(base))
				break;

    // *** Oculus HMD & Sensor Initialization

    // Create DeviceManager and first available HMDDevice from it.
    // Sensor object is created from the HMD, to ensure that it is on the
    // correct device.

    pManager = *DeviceManager::Create();

    // We'll handle it's messages in this case.
    pManager->SetMessageHandler(this);

    pHMD     = *pManager->EnumerateDevices<HMDDevice>().CreateDevice();
    if(pHMD)
    {
        pSensor = *pHMD->GetSensor();

        // This will initialize HMDInfo with information about configured IPD,
        // screen size and other variables needed for correct projection.
        // We pass HMD DisplayDeviceName into the renderer to select the
        // correct monitor in full-screen mode.
        if(pHMD->GetDeviceInfo(&HMDInfo))
        {
            //RenderParams.MonitorName = hmd.DisplayDeviceName;
            SConfig.SetHMDInfo(HMDInfo);
        }
    }
    else
    {
        // If we didn't detect an HMD, try to create the sensor directly.
        // This is useful for debugging sensor interaction; it is not needed in
        // a shipping app.
        pSensor = *pManager->EnumerateDevices<SensorDevice>().CreateDevice();
    }

    // Create the Latency Tester device and assign it to the LatencyTesterUtil object.
    pLatencyTester = *pManager->EnumerateDevices<LatencyTestDevice>().CreateDevice();
    if (pLatencyTester)
    {
        LatencyUtil.SetDevice(pLatencyTester);
    }
    // Make the user aware which devices are present.
	char devbuf[4096];
	memset(devbuf, 0, 4096);
	strcat(devbuf, "---------------------------------\n");
	if(pHMD == NULL)
		strcat(devbuf, "NO HMD DETECTED\n");
	if(pSensor == NULL)
		strcat(devbuf, "NO SENSOR DETECTED\n");
	if(!FoundHydra)
		strcat(devbuf, "NO HYDRA DETECTED\n");
	if(pHMD && pSensor)
		strcat(devbuf, "Press F9 for Full-Screen on Rift\n");
	
	strcat(devbuf, "---------------------------------\n");
	SetAdjustMessage(devbuf);

    // First message should be extra-long.
    SetAdjustMessageTimeout(10.0f);


    if(HMDInfo.HResolution > 0)
    {
        Width  = HMDInfo.HResolution;
        Height = HMDInfo.VResolution;
    }

    if(!pPlatform->SetupWindow(Width, Height))
        return 1;

    String Title = "Oculus World Demo";
    if(HMDInfo.ProductName[0])
    {
        Title += " : ";
        Title += HMDInfo.ProductName;
    }
    pPlatform->SetWindowTitle(Title);

    // Report relative mouse motion in OnMouseMove
    pPlatform->SetMouseMode(Mouse_Relative);
    
    if(pSensor)
    {
        // We need to attach sensor to SensorFusion object for it to receive
        // body frame messages and update orientation. SFusion.GetOrientation()
        // is used in OnIdle() to orient the view.
        SFusion.AttachToSensor(pSensor);

        SFusion.SetDelegateMessageHandler(this);
    }


    // *** Initialize Rendering

    const char* graphics = "d3d11";

    // Select renderer based on command line arguments.
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "-r") && i < argc - 1)
            graphics = argv[i + 1];
        else if(!strcmp(argv[i], "-fs"))
            RenderParams.Fullscreen = true;
    }

    // Enable multi-sampling by default.
    RenderParams.Multisample = 4;
    pRender = pPlatform->SetupGraphics(OVR_DEFAULT_RENDER_DEVICE_SET,
                                       graphics, RenderParams);



    // *** Configure Stereo settings.

    SConfig.SetFullViewport(Viewport(0, 0, Width, Height));
    SConfig.SetStereoMode(Stereo_LeftRight_Multipass);

    // Configure proper Distortion Fit.
    // For 7" screen, fit to touch left side of the view, leaving a bit of
    // invisible screen on the top (saves on rendering cost).
    // For smaller screens (5.5"), fit to the top.
    if (HMDInfo.HScreenSize > 0.0f)
        if (HMDInfo.HScreenSize > 0.140f)  // 7"
            SConfig.SetDistortionFitPointVP(-1.0f, 0.0f);        
        else        
            SConfig.SetDistortionFitPointVP(0.0f, 1.0f);

    pRender->SetSceneRenderScale(SConfig.GetDistortionScale());
    //pRender->SetSceneRenderScale(0.8f);

    SConfig.Set2DAreaFov(DegreeToRad(85.0f));


    // *** Identify Scene File & Prepare for Loading
   
    // This creates lights and models.
    if (argc == 2)
    {        
        MainFilePath = argv[1];
        PopulateLODFileNames();
    }
    else
    {
        fprintf(stderr, "Usage: OculusWorldDemo [input XML]\n");
        MainFilePath = WORLDDEMO_ASSET_FILE;	
    }

    // Try to modify path for correctness in case specified file is not found.
    if (!SysFile(MainFilePath).IsValid())
    {
        String prefixPath1(pPlatform->GetContentDirectory() + "/" + WORLDDEMO_ASSET_PATH1),
               prefixPath2(WORLDDEMO_ASSET_PATH2),
               prefixPath3(WORLDDEMO_ASSET_PATH3);
        if (SysFile(prefixPath1 + MainFilePath).IsValid())
            MainFilePath = prefixPath1 + MainFilePath;
        else if (SysFile(prefixPath2 + MainFilePath).IsValid())
            MainFilePath = prefixPath2 + MainFilePath;
        else if (SysFile(prefixPath3 + MainFilePath).IsValid())
            MainFilePath = prefixPath3 + MainFilePath;
    }

    PopulatePreloadScene();

    LastUpdate = pPlatform->GetAppTime();

    return 0;
}


void OculusWorldDemoApp::OnMessage(const Message& msg)
{
	if (msg.Type == Message_DeviceAdded && msg.pDevice == pManager)
	{
		LogText("DeviceManager reported device added.\n");
	}
	else if (msg.Type == Message_DeviceRemoved && msg.pDevice == pManager)
	{
		LogText("DeviceManager reported device removed.\n");
	}
	else if (msg.Type == Message_DeviceAdded && msg.pDevice == pSensor)
	{
		LogText("Sensor reported device added.\n");
	}
	else if (msg.Type == Message_DeviceRemoved && msg.pDevice == pSensor)
	{
		LogText("Sensor reported device removed.\n");
	}
}

void OculusWorldDemoApp::OnResize(int width, int height)
{
    Width  = width;
    Height = height;
    SConfig.SetFullViewport(Viewport(0, 0, Width, Height));
}

void OculusWorldDemoApp::OnGamepad(const GamepadState& pad)
{
    Player.GamepadMove   = Vector3f(pad.LX * pad.LX * (pad.LX > 0 ? 1 : -1),
                             0,
                             pad.LY * pad.LY * (pad.LY > 0 ? -1 : 1));
    Player.GamepadRotate = Vector3f(2 * pad.RX, -2 * pad.RY, 0);
}

void OculusWorldDemoApp::OnMouseMove(int x, int y, int modifiers)
{
    if(modifiers & Mod_MouseRelative)
    {
        // Get Delta
        int dx = x, dy = y;

        const float maxPitch = ((3.1415f / 2) * 0.98f);

        // Apply to rotation. Subtract for right body frame rotation,
        // since yaw rotation is positive CCW when looking down on XZ plane.
        Player.EyeYaw   -= (Sensitivity * dx) / 360.0f;

        if(!pSensor)
        {
            Player.EyePitch -= (Sensitivity * dy) / 360.0f;

            if(Player.EyePitch > maxPitch)
            {
                Player.EyePitch = maxPitch;
            }
            if(Player.EyePitch < -maxPitch)
            {
                Player.EyePitch = -maxPitch;
            }
        }
    }
}


void OculusWorldDemoApp::OnKey(KeyCode key, int chr, bool down, int modifiers)
{
    OVR_UNUSED(chr);

    switch(key)
    {
    case Key_Q:
        if(down && (modifiers & Mod_Control))
        {
            pPlatform->Exit(0);
        }
        break;

        // Handle player movement keys.
        // We just update movement state here, while the actual translation is done in OnIdle()
        // based on time.
    case Key_W:
        Player.MoveForward = down ? (Player.MoveForward | 1) : (Player.MoveForward & ~1);
        break;
    case Key_S:
        Player.MoveBack    = down ? (Player.MoveBack    | 1) : (Player.MoveBack    & ~1);
        break;
    case Key_A:
        Player.MoveLeft    = down ? (Player.MoveLeft    | 1) : (Player.MoveLeft    & ~1);
        break;
    case Key_D:
        Player.MoveRight   = down ? (Player.MoveRight   | 1) : (Player.MoveRight   & ~1);
        break;
    case Key_Up:
        Player.MoveForward = down ? (Player.MoveForward | 2) : (Player.MoveForward & ~2);
        break;
    case Key_Down:
        Player.MoveBack    = down ? (Player.MoveBack    | 2) : (Player.MoveBack    & ~2);
        break;
    case Key_Left:
        Player.MoveLeft    = down ? (Player.MoveLeft    | 2) : (Player.MoveLeft    & ~2);
        break;
    case Key_Right:
        Player.MoveRight   = down ? (Player.MoveRight   | 2) : (Player.MoveRight   & ~2);
        break;

    case Key_Minus:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustEyeHeight  : 0;
        AdjustDirection = -1;
        break;
    case Key_Equal:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustEyeHeight  : 0;
        AdjustDirection = 1;
        break;

    case Key_B:
        if(down)
        {
            if(SConfig.GetDistortionScale() == 1.0f)
            {
                if(SConfig.GetHMDInfo().HScreenSize > 0.140f)  // 7"
                {
                    SConfig.SetDistortionFitPointVP(-1.0f, 0.0f);
                }
                else
                {
                 SConfig.SetDistortionFitPointVP(0.0f, 1.0f);
                }
            }
            else
            {
                // No fitting; scale == 1.0.
                SConfig.SetDistortionFitPointVP(0, 0);
            }
        }
        break;

    // Support toggling background color doe distortion so that we can see
    // effect on the periphery.
    case Key_V:
        if(down)
        {
            if(DistortionClearColor.B == 0)
            {
                DistortionClearColor = Color(0, 128, 255);
            }
            else
            {
                DistortionClearColor = Color(0, 0, 0);
            }

            pRender->SetDistortionClearColor(DistortionClearColor);
        }
        break;


    case Key_F1:
        SConfig.SetStereoMode(Stereo_None);
        PostProcess = PostProcess_None;
        SetAdjustMessage("StereoMode: None");
        break;
    case Key_F2:
        SConfig.SetStereoMode(Stereo_LeftRight_Multipass);
        PostProcess = PostProcess_None;
        SetAdjustMessage("StereoMode: Stereo + No Distortion");
        break;
    case Key_F3:
        SConfig.SetStereoMode(Stereo_LeftRight_Multipass);
        PostProcess = PostProcess_Distortion;
        SetAdjustMessage("StereoMode: Stereo + Distortion");
        break;

    case Key_R:
        SFusion.Reset();
        SetAdjustMessage("Sensor Fusion Reset");
        break;

    case Key_Space:
        if(!down)
        {
            TextScreen = (enum TextScreen)((TextScreen + 1) % Text_Count);
        }
        break;

    case Key_F8:
        if(!down)
        {
			RenderParams = pRender->GetParams();
            RenderParams.Multisample = RenderParams.Multisample > 1 ? 1 : 4;
            pRender->SetParams(RenderParams);
            if(RenderParams.Multisample > 1)
            {
                SetAdjustMessage("Multisampling On");
            }
            else
            {
                SetAdjustMessage("Multisampling Off");
            }
        }
        break;
    case Key_F9:
        if(!down)
        {
            CycleDisplay();
        }
        break;

    case Key_F11:
        if (!down)
        {
            RenderParams = pRender->GetParams();
            RenderParams.MonitorName = SConfig.GetHMDInfo().DisplayDeviceName;
            pRender->SetParams(RenderParams);

            pPlatform->SetMouseMode(Mouse_Normal);            
            pPlatform->SetFullscreen(RenderParams, pRender->IsFullscreen() ? Display_Window : Display_FakeFullscreen);
            pPlatform->SetMouseMode(Mouse_Relative); // Avoid mode world rotation jump.
            // If using an HMD, enable post-process (for distortion) and stereo.
            if(RenderParams.MonitorName.GetLength() && pRender->IsFullscreen())
            {
                SConfig.SetStereoMode(Stereo_LeftRight_Multipass);
                PostProcess = PostProcess_Distortion;
            }
        }
        break;

    case Key_Escape:
        if(!down)
        {
            // switch to primary screen windowed mode
            pPlatform->SetFullscreen(RenderParams, Display_Window);
            RenderParams.MonitorName = pPlatform->GetScreenName(0);
            pRender->SetParams(RenderParams);
            Screen = 0;
        }
        break;

        // Stereo adjustments.
    case Key_BracketLeft:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustFov    : 0;
        AdjustDirection = 1;
        break;
    case Key_BracketRight:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustFov    : 0;
        AdjustDirection = -1;
        break;
    case Key_Insert:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustIPD    : 0;
        AdjustDirection = 1;
        break;
    case Key_Delete:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustIPD    : 0;
        AdjustDirection = -1;
        break;
    case Key_PageUp:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustAspect : 0;
        AdjustDirection = 1;
        break;
    case Key_PageDown:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustAspect : 0;
        AdjustDirection = -1;
        break;

        // Distortion correction adjustments
    case Key_H:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK0 : NULL;
        AdjustDirection = -1;
        break;
    case Key_Y:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK0 : NULL;
        AdjustDirection = 1;
        break;
    case Key_J:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK1 : NULL;
        AdjustDirection = -1;
        break;
    case Key_U:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK1 : NULL;
        AdjustDirection = 1;
        break;
    case Key_K:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK2 : NULL;
        AdjustDirection = -1;
        break;
    case Key_I:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK2 : NULL;
        AdjustDirection = 1;
        break;
    case Key_L:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK3 : NULL;
        AdjustDirection = -1;
        break;
    case Key_O:
        pAdjustFunc = down ? &OculusWorldDemoApp::AdjustDistortionK3 : NULL;
        AdjustDirection = 1;
        break;


    case Key_Tab:
        if(down)
        {
            float t0      = SConfig.GetDistortionK(0),
                  t1      = SConfig.GetDistortionK(1),
                  t2      = SConfig.GetDistortionK(2),
                  t3      = SConfig.GetDistortionK(3);
            float tESD    = SConfig.GetEyeToScreenDistance(),
                  taspect = SConfig.GetAspectMultiplier(),
                  tipd    = SConfig.GetIPD();

            if(SavedK0 > 0.0f)
            {
                SConfig.SetDistortionK(0, SavedK0);
                SConfig.SetDistortionK(1, SavedK1);
                SConfig.SetDistortionK(2, SavedK2);
                SConfig.SetDistortionK(3, SavedK3);
                SConfig.SetEyeToScreenDistance(SavedESD);
                SConfig.SetAspectMultiplier(SavedAspect);
                SConfig.SetIPD(SavedEyeDistance);

                SetAdjustMessage("Restored:\n"
                                 "ESD:\t120 %.3f\t350 Eye:\t490 %.3f\n"
                                 "K0: \t120 %.4f\t350 K2: \t490 %.4f\n"
                                 "K1: \t120 %.4f\t350 K3: \t490 %.4f",
                                 SavedESD, SavedEyeDistance,
                                 SavedK0, SavedK2,
                                 SavedK1, SavedK3);
            }
            else
            {
                SetAdjustMessage("Setting Saved");
            }

            SavedK0 = t0;
            SavedK1 = t1;
            SavedK2 = t2;
            SavedK3 = t3;
            SavedESD = tESD;
            SavedAspect = taspect;
            SavedEyeDistance = tipd;
        }
        break;
 
    case Key_G:
        if(down)
        {
            if(SceneMode == Scene_World)
            {
                SceneMode = Scene_Grid;
                SetAdjustMessage("Grid Only");
            }
            else if(SceneMode == Scene_Grid)
            {
                SceneMode = Scene_Both;
                SetAdjustMessage("Grid Overlay");
            }
            else if(SceneMode == Scene_Both)
            {
                SceneMode = Scene_World;
                SetAdjustMessage("Grid Off");
            }
            break;
        }

        // Holding down Shift key accelerates adjustment velocity.
    case Key_Shift:
        ShiftDown = down;
        break;

        // Reset the camera position in case we get stuck
    case Key_T:
        Player.EyePos = Vector3f(10.0f, 1.6f, 10.0f);
        break;

    case Key_C:
        if(!down)
        {
            UPInt numNodes = MainScene.Models.GetSize();
            for(UPInt i = 0; i < numNodes; i++)
            {
                Ptr<OVR::Render::Model> nodePtr = MainScene.Models[i];
                Render::Model*          pNode = nodePtr.GetPtr();
                if(pNode->IsCollisionModel)
                {
                    pNode->Visible = !pNode->Visible;
                }
            }
            break;
        }
    case Key_N:
        RaiseLOD();
        break;
    case Key_M:
        DropLOD();
        break;
    }
}

void OculusWorldDemoApp::OnIdle()
{
    double curtime = pPlatform->GetAppTime();
    float  dt      = float(curtime - LastUpdate);
    LastUpdate     = curtime;
	sixenseAllControllerData acd;
	if(FoundHydra)
	{
		sixenseSetActiveBase(0);
		sixenseAllControllerData acd;
		sixenseGetAllNewestData( &acd );
		sixenseUtils::getTheControllerManager()->update( &acd );
	}

    if (LoadingState == LoadingState_DoLoad)
    {
        PopulateScene(MainFilePath.ToCStr());
		if(FoundHydra)
			LoadingState = LoadingState_InitHydra;
		else
			LoadingState = LoadingState_Finished;
        return;
    }

	if(LoadingState == LoadingState_InitHydra && HydraSetupFinished)
	{
		LoadingState = LoadingState_Finished;
	}

    // If one of Stereo setting adjustment keys is pressed, adjust related state.
    if (pAdjustFunc)
    {
        (this->*pAdjustFunc)(dt * AdjustDirection * (ShiftDown ? 5.0f : 1.0f));
    }

    // Process latency tester results.
    const char* results = LatencyUtil.GetResultsString();
    if (results != NULL)
    {
        LogText("LATENCY TESTER: %s\n", results); 
    }

    // Have to place this as close as possible to where the HMD orientation is read.
    LatencyUtil.ProcessInputs();


    // Handle Sensor motion.
    // We extract Yaw, Pitch, Roll instead of directly using the orientation
    // to allow "additional" yaw manipulation with mouse/controller.
    if(pSensor)
    {
        Quatf    hmdOrient = SFusion.GetOrientation();

        float    yaw = 0.0f;
        hmdOrient.GetEulerAngles<Axis_Y, Axis_X, Axis_Z>(&yaw, &Player.EyePitch, &Player.EyeRoll);

        Player.EyeYaw += (yaw - Player.LastSensorYaw);
        Player.LastSensorYaw = yaw;

        // NOTE: We can get a matrix from orientation as follows:
        // Matrix4f hmdMat(hmdOrient);

        // Test logic - assign quaternion result directly to view:
        // Quatf hmdOrient = SFusion.GetOrientation();
        // View = Matrix4f(hmdOrient.Inverted()) * Matrix4f::Translation(-EyePos);
    }


    if(curtime >= NextFPSUpdate)
    {
        NextFPSUpdate = curtime + 1.0;
        FPS = FrameCounter;
        FrameCounter = 0;
    }
    FrameCounter++;

    if(FPS < 40)
    {
        ConsecutiveLowFPSFrames++;
    }
    else
    {
        ConsecutiveLowFPSFrames = 0;
    }

    if(ConsecutiveLowFPSFrames > 200)
    {
        DropLOD();
        ConsecutiveLowFPSFrames = 0;
    }

    Player.EyeYaw -= Player.GamepadRotate.x * dt;
	Player.HandleCollision(dt, &CollisionModels, &GroundCollisionModels, ShiftDown);

    if(!pSensor)
    {
        Player.EyePitch -= Player.GamepadRotate.y * dt;

        const float maxPitch = ((3.1415f / 2) * 0.98f);
        if(Player.EyePitch > maxPitch)
        {
            Player.EyePitch = maxPitch;
        }
        if(Player.EyePitch < -maxPitch)
        {
            Player.EyePitch = -maxPitch;
        }
    }
	if(FoundHydra)
	{
		// Sixense stuff
		int left_index = sixenseUtils::getTheControllerManager()->getIndex( sixenseUtils::ControllerManager::P1L );
		int right_index = sixenseUtils::getTheControllerManager()->getIndex( sixenseUtils::ControllerManager::P1R );

		int base = 0;
		int cont =0;
		bool startpressed = false;
		for( base=0; base<sixenseGetMaxBases(); base++ )
		{
			sixenseSetActiveBase(base);

			// Get the latest controller data
			sixenseGetAllNewestData( &acd );

			if(right_index > -1 && sixenseIsControllerEnabled( right_index ))
			{
				sixenseControllerData* data = &acd.controllers[right_index];
				OVR::Platform::GamepadState pad;
				if(!data->is_docked)// && !(data->buttons & SIXENSE_BUTTON_1))
				{
					pad.Buttons = data->buttons;
					pad.RT = data->trigger;
					if(!(data->buttons & SIXENSE_BUTTON_JOYSTICK))
					{
						pad.LY = data->joystick_y;
						pad.LX = data->joystick_x;
					}
					else
					{
						pad.RY = data->joystick_y;
						pad.RX = data->joystick_x;
					}

					ShiftDown = (data->buttons & SIXENSE_BUTTON_BUMPER);
					startpressed = (data->buttons & SIXENSE_BUTTON_START);
					if(data->buttons & SIXENSE_BUTTON_1 && !Button1Pressed)
					{
						HydraEnabled = !HydraEnabled;
						Button1Pressed = true;
					}
					else if(!(data->buttons & SIXENSE_BUTTON_1))
						Button1Pressed = false;
					/*moves camera based on right hydra rotation, buggy and stuff
					float rot[4];
					rot[0] = data->rot_quat[0];
					rot[1] = data->rot_quat[1];
					rot[2] = data->rot_quat[2];
					rot[3] = data->rot_quat[3];

					if(data->buttons & SIXENSE_BUTTON_START)
					{
						BaseHydraRotation[0] = rot[0];
						BaseHydraRotation[1] = rot[1];
						BaseHydraRotation[2] = rot[2];
						BaseHydraRotation[3] = rot[3];
					}

					rot[0] -= BaseHydraRotation[0];
					rot[1] -= BaseHydraRotation[1];
					rot[2] -= BaseHydraRotation[2];
					rot[3] -= BaseHydraRotation[3];

					pad.RY = -rot[3];// - BaseHydraRotation[3]);
					pad.RX = -rot[2];//((data->rot_quat[2] > 0 ? 0 - data->rot_quat[2] : data->rot_quat[2]) - BaseHydraRotation[2]);
				
					HydraControlRotation[0] = data->rot_quat[0];
					HydraControlRotation[1] = data->rot_quat[1];
					HydraControlRotation[2] = data->rot_quat[2];
					HydraControlRotation[3] = data->rot_quat[3];*/
				}
				OnGamepad(pad);
			}
			if(left_index > -1 && sixenseIsControllerEnabled( left_index ) && HydraEnabled)
			{
				HydraLeftPos.x = acd.controllers[cont].pos[0];
				HydraLeftPos.y = acd.controllers[cont].pos[1];
				HydraLeftPos.z = acd.controllers[cont].pos[2];

				if(startpressed)
				{
					BaseHydraLeftPos.x = HydraLeftPos.x;
					BaseHydraLeftPos.y = HydraLeftPos.y;
					BaseHydraLeftPos.z = HydraLeftPos.z;
				}

				HydraLeftPos.x = (int)(HydraLeftPos.x - BaseHydraLeftPos.x);
				HydraLeftPos.y = (int)(HydraLeftPos.y - BaseHydraLeftPos.y);
				HydraLeftPos.z = (int)(HydraLeftPos.z - BaseHydraLeftPos.z);

				if(HydraLeftPos.x < HYDRA_DEAD_ZONE && HydraLeftPos.x > -HYDRA_DEAD_ZONE) HydraLeftPos.x = 0;
				if(HydraLeftPos.y < HYDRA_DEAD_ZONE && HydraLeftPos.y > -HYDRA_DEAD_ZONE) HydraLeftPos.y = 0;
				if(HydraLeftPos.z < HYDRA_DEAD_ZONE && HydraLeftPos.z > -HYDRA_DEAD_ZONE) HydraLeftPos.z = 0;
			}
		}
	}
    // Rotate and position View Camera, using YawPitchRoll in BodyFrame coordinates.
    //
    Matrix4f rollPitchYaw = Matrix4f::RotationY(Player.EyeYaw) * Matrix4f::RotationX(Player.EyePitch) *
                            Matrix4f::RotationZ(Player.EyeRoll);
    Vector3f up      = rollPitchYaw.Transform(UpVector);
    Vector3f forward = rollPitchYaw.Transform(ForwardVector);
	Vector3f right = rollPitchYaw.Transform(RightVector);

    // Minimal head modeling; should be moved as an option to SensorFusion.
    float headBaseToEyeHeight     = 0.15f;  // Vertical height of eye from base of head
    float headBaseToEyeProtrusion = 0.09f;  // Distance forward of eye from base of head

    Vector3f eyeCenterInHeadFrame(0.0f, headBaseToEyeHeight, -headBaseToEyeProtrusion);
    
	if(HydraEnabled && FoundHydra)
	{
		Player.AdjustedEyePos = forward * -((HydraLeftPos.z/1000.f) * 2);
		Player.AdjustedEyePos.y = 0;
		Player.AdjustedEyePos += Player.EyePos;
		Player.AdjustedEyePos += UpVector * (HydraLeftPos.y/1000.f);
		Player.AdjustedEyePos += right * ((HydraLeftPos.x/1000.f)*2);
	}
	else
		Player.AdjustedEyePos = Player.EyePos;
	
	Vector3f shiftedEyePos = Player.AdjustedEyePos + rollPitchYaw.Transform(eyeCenterInHeadFrame);
    shiftedEyePos.y -= eyeCenterInHeadFrame.y; // Bring the head back down to original height
    View = Matrix4f::LookAtRH(shiftedEyePos, shiftedEyePos + forward, up);

    //  Transformation without head modeling.
    // View = Matrix4f::LookAtRH(EyePos, EyePos + forward, up);

    // This is an alternative to LookAtRH:
    // Here we transpose the rotation matrix to get its inverse.
    //  View = (Matrix4f::RotationY(EyeYaw) * Matrix4f::RotationX(EyePitch) *
    //                                        Matrix4f::RotationZ(EyeRoll)).Transposed() *
    //         Matrix4f::Translation(-EyePos);


    switch(SConfig.GetStereoMode())
    {
    case Stereo_None:
        Render(SConfig.GetEyeRenderParams(StereoEye_Center));
        break;

    case Stereo_LeftRight_Multipass:
        //case Stereo_LeftDouble_Multipass:
        Render(SConfig.GetEyeRenderParams(StereoEye_Left));
        Render(SConfig.GetEyeRenderParams(StereoEye_Right));
        break;

    }

    pRender->Present();
    // Force GPU to flush the scene, resulting in the lowest possible latency.
    pRender->ForceFlushGPU();
}

static const char* HelpText =
    "F1\t100 NoStereo   \t420 Ins/Del \t630 ViewOffset\n"
    "F2\t100 Stereo     \t420 [ ]     \t630 FOV\n"
    "F3\t100 StereoHMD  \t420 H-Y J-U \t630 Distortion 1,3\n"
    "F4\t100 MonoHMD    \t420 K-I L-O \t630 Distortion 5,7\n"
    "F8\t100 MSAA       \t420 Shift   \t630 Adjust Faster\n"
    "F9\t100 FullScreen \t420 F11     \t630 Fast FullScreen\n"
	"- +\t100 Adjust EyeHeight\n"
    "R \t100 Reset SensorFusion"    
    ;


enum DrawTextCenterType
{
    DrawText_NoCenter= 0,
    DrawText_VCenter = 0x1,
    DrawText_HCenter = 0x2,
    DrawText_Center  = DrawText_VCenter | DrawText_HCenter
};

static void DrawTextBox(RenderDevice* prender, float x, float y,
                        float textSize, const char* text,
                        DrawTextCenterType centerType = DrawText_NoCenter)
{
    float ssize[2] = {0.0f, 0.0f};

    prender->MeasureText(&DejaVu, text, textSize, ssize);

    // Treat 0 a VCenter.
    if (centerType & DrawText_HCenter)
    {
        x = -ssize[0]/2;
    }
    if (centerType & DrawText_VCenter)
    {
        y = -ssize[1]/2;
    }

    prender->FillRect(x-0.02f, y-0.02f, x+ssize[0]+0.02f, y+ssize[1]+0.02f, Color(40,40,100,210));
    prender->RenderText(&DejaVu, text, x, y, textSize, Color(255,255,0,210));
}

void OculusWorldDemoApp::Render(const StereoEyeParams& stereo)
{
    pRender->BeginScene(PostProcess);

    // *** 3D - Configures Viewport/Projection and Render
    pRender->ApplyStereoParams(stereo);    
    pRender->Clear();

    pRender->SetDepthMode(true, true);
    if (SceneMode != Scene_Grid)
    {
        MainScene.Render(pRender, stereo.ViewAdjust * View);
    }


    // *** 2D Text & Grid - Configure Orthographic rendering.

    // Render UI in 2D orthographic coordinate system that maps [-1,1] range
    // to a readable FOV area centered at your eye and properly adjusted.
    pRender->ApplyStereoParams2D(stereo);    
    pRender->SetDepthMode(false, false);

    float unitPixel = SConfig.Get2DUnitPixel();
    float textHeight= unitPixel * 22; 

    if (SceneMode != Scene_World)
    {   // Draw grid two pixels thick.
        GridScene.Render(pRender, Matrix4f());
        GridScene.Render(pRender, Matrix4f::Translation(unitPixel,unitPixel,0));
    }

    // Display Loading screen-shot in frame 0.
    if (LoadingState != LoadingState_Finished && LoadingState != LoadingState_InitHydra)
    {
        LoadingScene.Render(pRender, Matrix4f());
        String loadMessage = String("Loading ") + MainFilePath;
        DrawTextBox(pRender, 0, 0.25f, textHeight, loadMessage.ToCStr(), DrawText_HCenter);
        LoadingState = LoadingState_DoLoad;
    }

	if(LoadingState == LoadingState_InitHydra)
	{
		DrawTextBox(pRender, 0, 1, textHeight, HydraTextString, DrawText_HCenter);
		//return;
	}

    if(AdjustMessageTimeout > pPlatform->GetAppTime())
    {
        DrawTextBox(pRender,0,0.5f, textHeight, AdjustMessage.ToCStr(), DrawText_HCenter);
    }

    switch(TextScreen)
    {
    case Text_Orientation:
    {
        char buf[256];
        size_t texMemInMB = pRender->GetTotalTextureMemoryUsage() / 1058576;
        OVR_sprintf(buf, sizeof(buf),
                    " Yaw:%4.0f  Pitch:%4.0f  Roll:%4.0f \n"
                    " FPS: %d  Frame: %d \n Pos: %3.2f, %3.2f, %3.2f \n"
					" HX: %3.2f, %3.2f, %3.2f \n"
					" RX: %3.2f, %3.2f, %3.2f, %3.2f \n"
                    " GPU Tex: %u MB \n EyeHeight: %3.2f",
                    RadToDegree(Player.EyeYaw), RadToDegree(Player.EyePitch), RadToDegree(Player.EyeRoll),
                    FPS, FrameCounter, Player.EyePos.x, Player.EyePos.y, Player.EyePos.z, 
					(HydraLeftPos.x/1000.f), (HydraLeftPos.y/1000.f), (HydraLeftPos.z/1000.f), 
					HydraControlRotation[0], HydraControlRotation[1], HydraControlRotation[2], HydraControlRotation[3],
					
					texMemInMB, Player.AdjustedEyePos.y);
            DrawTextBox(pRender, 0, 0.05f, textHeight, buf, DrawText_HCenter);
    }
    break;

    case Text_Config:
    {
        char   textBuff[2048];
         
        OVR_sprintf(textBuff, sizeof(textBuff),
                    "Fov\t300 %9.4f\n"
                    "EyeDistance\t300 %9.4f\n"
                    "DistortionK0\t300 %9.4f\n"
                    "DistortionK1\t300 %9.4f\n"
                    "DistortionK2\t300 %9.4f\n"
                    "DistortionK3\t300 %9.4f\n"
                    "TexScale\t300 %9.4f",
                    SConfig.GetYFOVDegrees(),
                        SConfig.GetIPD(),
                    SConfig.GetDistortionK(0),
                    SConfig.GetDistortionK(1),
                    SConfig.GetDistortionK(2),
                    SConfig.GetDistortionK(3),
                    SConfig.GetDistortionScale());

            DrawTextBox(pRender, 0, 0, textHeight, textBuff, DrawText_Center);
    }
    break;

    case Text_Help:
        DrawTextBox(pRender, 0, 0, textHeight, HelpText, DrawText_Center);
    }


    // Display colored quad if we're doing a latency test.
    Color colorToDisplay;
    if (LatencyUtil.DisplayScreenColor(colorToDisplay))
    {
        pRender->FillRect(-0.4f, -0.4f, 0.4f, 0.4f, colorToDisplay);
    }

    pRender->FinishScene();
}


// Sets temporarily displayed message for adjustments
void OculusWorldDemoApp::SetAdjustMessage(const char* format, ...)
{
    char textBuff[2048];
    va_list argList;
    va_start(argList, format);
    OVR_vsprintf(textBuff, sizeof(textBuff), format, argList);
    va_end(argList);

    // Message will time out in 4 seconds.
    AdjustMessage = textBuff;
    AdjustMessageTimeout = pPlatform->GetAppTime() + 4.0f;
}

void OculusWorldDemoApp::SetAdjustMessageTimeout(float timeout)
{
    AdjustMessageTimeout = pPlatform->GetAppTime() + timeout;
}

// ***** View Control Adjustments

void OculusWorldDemoApp::AdjustFov(float dt)
{
    float esd = SConfig.GetEyeToScreenDistance() + 0.01f * dt;
    SConfig.SetEyeToScreenDistance(esd);
    SetAdjustMessage("ESD:%6.3f  FOV: %6.3f", esd, SConfig.GetYFOVDegrees());
}

void OculusWorldDemoApp::AdjustAspect(float dt)
{
    float rawAspect = SConfig.GetAspect() / SConfig.GetAspectMultiplier();
    float newAspect = SConfig.GetAspect() + 0.01f * dt;
    SConfig.SetAspectMultiplier(newAspect / rawAspect);
    SetAdjustMessage("Aspect: %6.3f", newAspect);
}

void OculusWorldDemoApp::AdjustDistortion(float dt, int kIndex, const char* label)
{
    SConfig.SetDistortionK(kIndex, SConfig.GetDistortionK(kIndex) + 0.03f * dt);
    SetAdjustMessage("%s: %6.4f", label, SConfig.GetDistortionK(kIndex));
}

void OculusWorldDemoApp::AdjustIPD(float dt)
{
    SConfig.SetIPD(SConfig.GetIPD() + 0.0025f * dt);
    SetAdjustMessage("EyeDistance: %6.4f", SConfig.GetIPD());
}

void OculusWorldDemoApp::AdjustEyeHeight(float dt)
{
    float dist = 0.5f * dt;

    Player.EyeHeight += dist;
    Player.EyePos.y += dist;

    SetAdjustMessage("EyeHeight: %4.2f", Player.EyeHeight);
}


// Loads the scene data
void OculusWorldDemoApp::PopulateScene(const char *fileName)
{    
    XmlHandler xmlHandler;     
    if(!xmlHandler.ReadFile(fileName, pRender, &MainScene, &CollisionModels, &GroundCollisionModels))
    {
        SetAdjustMessage("---------------------------------\nFILE LOAD FAILED\n---------------------------------");
        SetAdjustMessageTimeout(10.0f);
    }    

    MainScene.SetAmbient(Vector4f(1.0f, 1.0f, 1.0f, 1.0f));
    
    // Distortion debug grid (brought up by 'G' key).
    Ptr<Model> gridModel = *Model::CreateGrid(Vector3f(0,0,0), Vector3f(1.0f/10, 0,0), Vector3f(0,1.0f/10,0),
                                              10, 10, 5, 
                                              Color(0, 255, 0, 255), Color(255, 50, 50, 255) );
    GridScene.World.Add(gridModel);
}


void OculusWorldDemoApp::PopulatePreloadScene()
{
    // Load-screen screen shot image
    String fileName = MainFilePath;
    fileName.StripExtension();

    Ptr<File>    imageFile = *new SysFile(fileName + "_LoadScreen.tga");
    Ptr<Texture> imageTex;
    if (imageFile->IsValid())
        imageTex = *LoadTextureTga(pRender, imageFile);

    // Image is rendered as a single quad.
    if (imageTex)
    {
        imageTex->SetSampleMode(Sample_Anisotropic|Sample_Repeat);
        Ptr<Model> m = *new Model(Prim_Triangles);        
        m->AddVertex(-0.5f,  0.5f,  0.0f, Color(255,255,255,255), 0.0f, 0.0f);
        m->AddVertex( 0.5f,  0.5f,  0.0f, Color(255,255,255,255), 1.0f, 0.0f);
        m->AddVertex( 0.5f, -0.5f,  0.0f, Color(255,255,255,255), 1.0f, 1.0f);
        m->AddVertex(-0.5f, -0.5f,  0.0f, Color(255,255,255,255), 0.0f, 1.0f);
        m->AddTriangle(2,1,0);
        m->AddTriangle(0,3,2);

        Ptr<ShaderFill> fill = *new ShaderFill(*pRender->CreateShaderSet());
        fill->GetShaders()->SetShader(pRender->LoadBuiltinShader(Shader_Vertex, VShader_MVP)); 
        fill->GetShaders()->SetShader(pRender->LoadBuiltinShader(Shader_Fragment, FShader_Texture)); 
        fill->SetTexture(0, imageTex);
        m->Fill = fill;

        LoadingScene.World.Add(m);
    }
}

void OculusWorldDemoApp::ClearScene()
{
    MainScene.Clear();
    GridScene.Clear();
}

void OculusWorldDemoApp::PopulateLODFileNames()
{
    //OVR::String mainFilePath = MainFilePath;
    LODFilePaths.PushBack(MainFilePath);
    int   LODIndex = 1;
    SPInt pos = strcspn(MainFilePath.ToCStr(), ".");
    SPInt len = strlen(MainFilePath.ToCStr());
    SPInt diff = len - pos;

    if (diff == 0)
        return;    

    while(true)
    {
        char pathWithoutExt[250];
        char buffer[250];
        for(SPInt i = 0; i < pos; ++i)
        {
            pathWithoutExt[i] = MainFilePath[(int)i];
        }
        pathWithoutExt[pos] = '\0';
        OVR_sprintf(buffer, sizeof(buffer), "%s%i.xml", pathWithoutExt, LODIndex);
        FILE* fp = 0;
#if defined(_MSC_VER) && (_MSC_VER >= 1400 )
        errno_t err = fopen_s(&fp, buffer, "rb");
        if(!fp || err)
        {
#else
        fp = fopen(buffer, "rb");
        if(!fp)
        {
#endif
            break;
        }
        fclose(fp);
        OVR::String result = buffer;
        LODFilePaths.PushBack(result);
        LODIndex++;
    }
}

void OculusWorldDemoApp::DropLOD()
{
    if(CurrentLODFileIndex < (int)(LODFilePaths.GetSize() - 1))
    {
        ClearScene();
        CurrentLODFileIndex++;
        PopulateScene(LODFilePaths[CurrentLODFileIndex].ToCStr());
    }
}

void OculusWorldDemoApp::RaiseLOD()
{
    if(CurrentLODFileIndex > 0)
    {
        ClearScene();
        CurrentLODFileIndex--;
        PopulateScene(LODFilePaths[CurrentLODFileIndex].ToCStr());
    }
}

//-----------------------------------------------------------------------------
void OculusWorldDemoApp::CycleDisplay()
{
    int screenCount = pPlatform->GetScreenCount();

    // If Windowed, switch to the HMD screen first in Full-Screen Mode.
    // If already Full-Screen, cycle to next screen until we reach FirstScreenInCycle.

    if (pRender->IsFullscreen())
    {
        // Right now, we always need to restore window before going to next screen.
        pPlatform->SetFullscreen(RenderParams, Display_Window);

        Screen++;
        if (Screen == screenCount)
            Screen = 0;

        RenderParams.MonitorName = pPlatform->GetScreenName(Screen);

        if (Screen != FirstScreenInCycle)
        {
            pRender->SetParams(RenderParams);
            pPlatform->SetFullscreen(RenderParams, Display_Fullscreen);
        }
    }
    else
    {
        // Try to find HMD Screen, making it the first screen in full-screen Cycle.        
        FirstScreenInCycle = 0;
        if (pHMD)
        {   
            for (int i = 0; i< screenCount; i++)
            {   
                String screenName = pPlatform->GetScreenName(i);
                // Under Windows, diplay (screenName )is a part of the full monitor string
                if (strstr(SConfig.GetHMDInfo().DisplayDeviceName, screenName))
                {
                    FirstScreenInCycle = i;
                    break;
                }
            }            
        }

        // Switch full-screen on the HMD.
        Screen = FirstScreenInCycle;
        RenderParams.MonitorName = pPlatform->GetScreenName(Screen);
        pRender->SetParams(RenderParams);
        pPlatform->SetFullscreen(RenderParams, Display_Fullscreen);
    }
}

//-------------------------------------------------------------------------------------

OVR_PLATFORM_APP(OculusWorldDemoApp);

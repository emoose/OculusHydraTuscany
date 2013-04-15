OculusHydraTuscany
==================

Oculus Rift Tuscany demo modded to use Razer Hydra for headtracking

Install
=======

- Copy Assets folder from ovr_sdk_0.1.5_src\OculusSDK\Samples\OculusWorldDemo\Assets to OculusWorldDemo\Assets
- (requires VS2010): build solution and debug
- (doesn't require VS): run the exe in OculusWorldDemo\Release

Usage
=====

- Make sure latest Sixense drivers are installed
- Put the hydra base directly in front center of you
- Place both controllers on the hydra base
- EXIT SIXENSE MOTIONCREATOR / HYDRA SOFTWARE
- Start the demo
- Set eyeposition to your height in meters using - and = keys
- Attach left controller to head somehow
- Stand up and press start button on right controller
- Duck/jump/run/fly/whatever, and test test test!

Controls
========

Left Controller - Headtracking

Right Controller:
- Bumper - Sprint
- Joystick - Move
- Joystick Pressed - Rotate camera
- Button 1 - Enable/disable headtracking
- START - Reset standing height

Keyboard:
- F1 - No stereo display
- F2 - Stereo display
- F3 - Stereo display + distortion
- SPACE - Stats (HX/RX lines are Hydra info)

Todo
====

Test with actual Rift (won't be too long hopefully!)
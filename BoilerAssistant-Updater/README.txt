Boiler Assistant Windows Updater

User steps:
1. Connect the UNO R4 WiFi to USB.
2. Close Arduino IDE and serial monitors.
3. Double-click Update-BoilerAssistant.cmd.
4. Press Enter when prompted.

The updater downloads the newest firmware from GitHub automatically, then
uploads it. It does not require the user to install Arduino IDE or compile
source code. If there is no internet connection, it falls back to any .hex
file already sitting in the firmware folder.

Publishing a new version (for the maintainer):
1. Compile a release .hex file.
2. Put ONLY that one .hex file in the GitHub folder configured in
   Update-BoilerAssistant.ps1 (repoOwner/repoName/repoBranch/repoFolder).
3. Remove any older .hex file from that folder so only the newest remains.
Every user's updater will then fetch that file the next time they run it.

Package contents:
- firmware\: holds the downloaded (or fallback) release firmware image
- tools\arduino-cli.exe: bundled Arduino command-line uploader
- tools\arduino-data\: bundled Arduino UNO R4 board package data

Do not unplug the controller during upload. If automatic detection fails, enter the COM port shown by Windows.

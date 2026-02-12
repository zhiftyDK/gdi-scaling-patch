## GDI Scaling Patch

Some older GDI games doesnt scale to fullscreen on modern computers and monitors. This patch will re-scale the game to fit on higher resolution computers in fullscreen mode. Download the <strong>gdi_scaling_patch.zip</strong> and add <strong>winmm.dll</strong> + <strong>winmm_orig.dll</strong> next to your .exe file.

Download Latest: [https://github.com/zhiftyDK/gdi-scaling-patch/releases/latest](https://github.com/zhiftyDK/gdi-scaling-patch/releases/latest)

#### Example of where to put <strong>winmm.dll</strong>:
```bash
C:\Program Files (x86)\My Game\
    │   MyGame.exe
    │   winmm.dll <- Put the winmm.dll file here in the root of your game folder
    ├───folder1
    ├───folder2
    └───folder3
```

#### Compile source:

```bash
# Run build.bat inside x86 Native Tools Command Prompt for VS
# OR
# Compile using MSVC
cl /LD /O2 /DNDEBUG winmm.cpp /link /OUT:winmm.dll gdi32.lib user32.lib
```

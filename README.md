## GDI Scaling Patch

Some older GDI games doesnt scale to fullscreen on modern computers and monitors. This patch will re-scale the game to fit on higher resolution computers in fullscreen mode. Download the <strong>gdi-scaling-patch.zip</strong> and add <strong>winmm.dll</strong> + <strong>winmm_orig.dll</strong> next to your .exe file.

Download Latest: [https://github.com/zhiftyDK/gdi-scaling-patch/releases/latest](https://github.com/zhiftyDK/gdi-scaling-patch/releases/latest)

#### Example of where to put <strong>winmm.dll</strong> and <strong>winmm_orig.dll</strong>:
```bash
C:\Program Files (x86)\My Game\
    │   MyGame.exe
    │   winmm.dll <- Put the winmm.dll file here in the root of your game folder
    │   winmm_orig.dll <- Put the winmm_orig.dll file here in the root of your game folder
    ├───folder1
    ├───folder2
    └───folder3
```

#### Compile source:

```bash
# Compile using WinGW
g++ -shared -m32 -O2 winmm.cpp winmm.def -o winmm.dll -lgdi32 -luser32 -static-libgcc -static-libstdc++
```

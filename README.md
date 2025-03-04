# Jewena Chinese Patch
## How to Compile
```powershell
md build && cd build
cmake -A x64 ../
cmake --build . --config Release
```
Now you can find two files, `jewena_patch.dll` and `jewena-chs.exe`, in the `build/Release` directory.

## How to Use
Use zip to package the resource files into `jewena-chs.dat`.  
Then copy `jewena_patch.dll`, `jewena-chs.dat` and `jewena-chs.exe` to the game directory.  
You can change resource package file name in source code.

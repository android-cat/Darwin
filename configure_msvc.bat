@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.10.2/msvc2022_64" -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/Ninja/ninja.exe" > cmake_config.log 2>&1
echo CMAKE_EXIT=%ERRORLEVEL%

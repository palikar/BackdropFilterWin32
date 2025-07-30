workspace "BackdropFilterWin32"

configurations { "Debug", "Release" }
platforms { "x64" }

characterset 'MBCS'
cppdialect "C++17"
architecture "x86_64"
rtti "Off"
exceptionhandling "Off"
vectorextensions "SSE"
vectorextensions "SSE2"
vectorextensions "AVX2"
floatingpoint "Fast"
pic "On"
systemversion "latest"
editandcontinue "Off"
disablewarnings {
   "4201",
   "4100",
   "4189",
   "4505",
   "4127",
   "4245",
   "4244",
}
warnings "Off"
filter "files:**.rec"
buildaction "ResourceCompile"
filter{}

filter "files:**.natvis"
buildaction "Natvis"
filter{}

buildoptions {
   "/permissive-",
}

project "BackdropFilterWin32"
language "C++"
kind "WindowedApp"

targetdir "./Build/$(Configuration)/"
objdir "./Build/$(Configuration)/$(ProjectName)"

files {
   "./BackdropFilterWin32.cpp",
}

links {
   "kernel32.lib",
   "user32.lib",
   "gdi32.lib",
   "winspool.lib",
   "comdlg32.lib",
   "advapi32.lib",
   "shell32.lib",
   "ole32.lib",
   "oleaut32.lib",
   "uuid.lib",
   "odbc32.lib",
   "odbccp32.lib",
   "d3d11.lib",
   "D3DCompiler.lib",
   "shlwapi.lib",
   "dxguid.lib",
   "Mincore.lib",
   "dxgi.lib",
   "winmm.lib",
}

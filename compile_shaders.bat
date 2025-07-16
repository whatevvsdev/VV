@echo off

set SHADER_COMPILER=%VULKAN_SDK%\Bin\glslc.exe

set ROOT_FOLDER="shaders"
set OUTPUT_FOLDER=%ROOT_FOLDER%\spirv-out
set EXTENSIONS=comp frag vert

if not exist "%OUTPUT_FOLDER%" (
    mkdir "%OUTPUT_FOLDER%"
)

for %%e in (%EXTENSIONS%) do (
    for /R "%ROOT_FOLDER%" %%f in (*.%%e) do (
        %SHADER_COMPILER% -g "%%f" -o "%OUTPUT_FOLDER%\%%~nxf.spv"
    )
)
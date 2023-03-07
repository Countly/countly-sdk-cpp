@echo off
if /i "%CD:~-5%"=="build" (
    echo In build folder. Changing directory...
    cd ..
    echo Current directory: %CD%
) else (
    echo Not in build folder. Current directory: %CD%
)
echo Deleting folder and its subfolders...
rmdir /s /q build
echo Folder and subfolders deleted.
cmake -DCOUNTLY_BUILD_SAMPLE=1 -DCOUNTLY_BUILD_TESTS=1 -DCMAKE_VERBOSE_MAKEFILE=OFF -DCMAKE_FIND_DEBUG_MODE=ON -B build . -G "Unix Makefiles"
cd build
make ./countly-sample
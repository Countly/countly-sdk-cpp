#!/bin/bash

if [ "${PWD##*/}" = "build" ]
then
    echo "In build folder. Changing directory..."
    cd ..
    echo "Current directory: $PWD"
else
    echo "Not in build folder. Current directory: $PWD"
fi

echo "Deleting folder and its subfolders..."
rm -rf build
echo "Folder and subfolders deleted."

cmake -DCOUNTLY_BUILD_SAMPLE=1 -DCOUNTLY_BUILD_TESTS=1 -DCOUNTLY_USE_CUSTOM_SHA256=OFF -DCOUNTLY_USE_SQLITE=OFF -B build .

cd build
make ./countly-sample
make ./countly-tests

#!/bin/sh

# Taken from https://gitlab.com/postmarketOS/fbdebug/blob/master/.gitlab-ci/static_clang-format.sh
# Please keep in sync

set -e

clang-format -i $(find . -name "*.c" -or -iname "*.h")

git diff > differences.patch

if [ -s differences.patch ]; then
    echo "C-Code formatting check failed!"
    echo "Please make sure, the code is formatted properly!"
    echo "Run:"
    echo '    clang-format -i $(find . -name "*.c" -or -iname "*.h")'
    echo
    cat differences.patch
    echo
    rm differences.patch
    exit 1
else
    echo "C-Code formatting check is sucessful!"
    exit 0
fi


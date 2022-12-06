#!/bin/bash

set -xe

git fetch origin
git checkout release
git reset --hard release
git merge develop --squash --allow-unrelated-histories -X theirs
git commit
git push origin release
git checkout develop
echo Done!

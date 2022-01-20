#!/bin/bash

set -xe

git fetch origin
git checkout master
git reset --hard master
git merge develop --squash --allow-unrelated-histories -X theirs
git commit
git push origin master
echo Done!

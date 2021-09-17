/*
 * This file was taken from RakNet 4.082.
 * Please see licenses/RakNet license.txt for the underlying license and related copyright.
 *
 * Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
 *
 * This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 * license found in the license.txt file in the root directory of this source tree.
 */

#ifdef _WIN32
typedef long off_t;
#endif

/// Given \a old and \a new , return \a out which will contain a patch to get from \a old to \a new .  \a out is allocated for you.
bool CreatePatch(const char *old, off_t oldsize, char *_new, off_t newsize, char **out, unsigned *outSize);
bool CreatePatch(const char *old, int oldsize, char *_new, int newsize, char **out, unsigned *outSize);
bool CreatePatch(const char *old, unsigned oldsize, char *_new, unsigned int newsize, char **out, unsigned *outSize);
bool CreatePatch(const char *old, int oldsize, char *_new, unsigned int newsize, char **out, unsigned *outSize);
bool CreatePatch(const char *old, unsigned oldsize, char *_new, int newsize, char **out, unsigned *outSize);

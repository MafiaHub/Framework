/*
 * This file is a modified version of the sourcecode which is part of miniupnp.
 * Please see licenses/MiniUPnP License.txt for the underlying license and related copyright.
 *
 * This file was taken from RakNet 4.082.
 * Please see licenses/RakNet license.txt for the underlying license and related copyright.
 *
 * Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
 *
 * This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 * license found in the license.txt file in the root directory of this source tree.
 */

#ifndef __DECLSPEC_H__
#define __DECLSPEC_H__

// KevinJ: Use RakNet's more sophisticated export
#include "slikenet/Export.h"
#define LIBSPEC RAK_DLL_EXPORT
/*
#if defined(WIN32) && !defined(STATICLIB)
	#ifdef MINIUPNP_EXPORTS
		#define LIBSPEC __declspec(dllexport)
	#else
		#define LIBSPEC __declspec(dllimport)
	#endif
#else
	#define LIBSPEC
#endif
*/

#endif


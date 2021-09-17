/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2016-2018, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#ifndef __PROFANITY_FILTER__H__
#define __PROFANITY_FILTER__H__

#include "slikenet/DS_List.h"
#include "slikenet/string.h"

namespace SLNet {

class ProfanityFilter
{
public:
	ProfanityFilter();
	~ProfanityFilter();

	// Returns true if the string has profanity, false if not.
	bool HasProfanity(const char *str);

	// Removes profanity. Returns number of occurrences of profanity matches (including 0)
	int FilterProfanity(const char *input, char *output, bool filter = true);
	int FilterProfanity(const char *input, char *output, size_t outputLength, bool filter = true); 		
	
	// Number of profanity words loaded
	int Count();

	void AddWord(SLNet::RakString newWord);
private:	
	DataStructures::List<SLNet::RakString> words;

	char RandomBanChar();

	static char BANCHARS[];
	static char WORDCHARS[];
};

} // namespace SLNet

#endif // __PROFANITY__H__

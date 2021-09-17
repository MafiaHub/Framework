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

#include "ProfanityFilter.h"
#include "slikenet/Rand.h"
#include "slikenet/assert.h"
#include "slikenet/LinuxStrings.h"
#include "slikenet/linux_adapter.h"
#include "slikenet/osx_adapter.h"

#if defined(_WIN32)
#include <malloc.h> // alloca
#elif (defined(__GNUC__)  || defined(__GCCXML__))
#include <alloca.h>
#else
#endif

using namespace SLNet;

char ProfanityFilter::BANCHARS[] = "!@#$%^&*()";
char ProfanityFilter::WORDCHARS[] = "abcdefghijklmnopqrstuvwxyz0123456789";

ProfanityFilter::ProfanityFilter()
{
}

ProfanityFilter::~ProfanityFilter()
{

}

char ProfanityFilter::RandomBanChar()
{
	return BANCHARS[randomMT() % (sizeof(BANCHARS) - 1)];
}


bool ProfanityFilter::HasProfanity(const char *str)
{
	return FilterProfanity(str, 0, 0,false) > 0;
}

int ProfanityFilter::FilterProfanity(const char *input, char *output, bool filter)
{
	if (input == 0 || input[0] == 0)
		return 0;

	int count = 0;
	char* b = (char *)alloca(strlen(input) + 1);
	strcpy_s(b, strlen(input) + 1, input);
	_strlwr(b);
	char *start = b;
	if (output)
		strcpy(output, input);

	start = strpbrk(start, WORDCHARS);
	while (start != 0)
	{
		size_t len = strspn(start, WORDCHARS);
		if (len > 0)
		{
			// we a have a word - let's check if it's a BAAAD one
			char saveChar = start[len];
			start[len] = '\0';

			// loop through profanity list			
			for (unsigned int i = 0, size = words.Size(); i < size; i++)
			{
				if (_stricmp(start, words[i].C_String()) == 0)
				{
					count++;

					// size_t len = words[i].size();
					if (filter && output)
					{
						for (unsigned int j = 0; j < len; j++)
						{
							output[start + j - b] = RandomBanChar();
						}
					}
					break;
				}
			}
			start[len] = saveChar;
		}

		start += len;
		start = strpbrk(start, WORDCHARS);
	}

	return count;
}

int ProfanityFilter::FilterProfanity(const char *input, char *output, size_t outputLength, bool filter)
{
	if (input==0 || input[0]==0)
		return 0;

	int count = 0;
	char* b = (char *) alloca(strlen(input) + 1); 
	strcpy_s(b, strlen(input) + 1, input);
	_strlwr(b);
	char *start = b;
	if (output)
		strcpy_s(output,outputLength,input);

	start = strpbrk(start, WORDCHARS);
	while (start != 0)
	{
		size_t len = strspn(start, WORDCHARS);
		if (len > 0)
		{
			// we a have a word - let's check if it's a BAAAD one
			char saveChar = start[len];
			start[len] = '\0';

			// loop through profanity list			
			for (unsigned int i = 0, size = words.Size(); i < size; i++)
			{
				if (_stricmp(start, words[i].C_String()) == 0)
				{
					count++;

					// size_t len = words[i].size();
					if (filter && output)
					{
						for (unsigned int j = 0; j < len; j++)
						{
							output[start + j - b] = RandomBanChar();
						}
					}
					break;
				}				
			}
			start[len] = saveChar;
		}

		start += len;
		start = strpbrk(start, WORDCHARS);
	}

	return count;
}

int ProfanityFilter::Count()
{
	return words.Size();
}
void ProfanityFilter::AddWord(SLNet::RakString newWord)
{
	words.Insert(newWord, _FILE_AND_LINE_ );
}

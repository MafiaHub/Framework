/*
*  Copyright (c) 2018-2019, SLikeSoft UG (haftungsbeschraenkt)
*
*  This source code is licensed under the MIT-style license found in the license.txt
*  file in the root directory of this source tree.
*/

#pragma once
#include <cstddef> // required for size_t

namespace MafiaNet
{
	namespace Experimental
	{
		namespace Crypto
		{
			// #med - consider CSecureMemoryBuffer and derive CSecureString from that class
			//        difference would be implicit null-terminated buffer in string buffer (upon Decrypt calls)
			// document: document Decrypt/FlushUnencryptedData() requirements for most secure handling
			//           i.e. emphasize that FlushUnencryptedData() must be called after having called Decrypt() ASAP once access to the unencrypted data
			//           data is no longer required
			class CSecureString
			{
				// member variables
			private:
				bool m_UTF8Mode;
				bool m_wasFlushed;
				size_t m_EncryptedBufferSize;    // size of the buffer for the encrypted string
				size_t m_numBufferSize;          // size of the actual supported string buffer (excluding the trailing \0-terminator)
				size_t m_numBufferUsed;          // size of the available buffer currently used
				size_t m_numEncryptedBufferUsed; // size of the encrypted buffer which is used and contains the encrypted data
				size_t m_UnencryptedBufferSize;  // size of the buffer allocated to retrieve the decrypted string
				unsigned char* m_EncryptedMemory;
				char* m_UnencryptedBuffer;

				// constructor / destructor
			public:
				CSecureString(const size_t maxBufferSize, const bool utf8Mode = false);
				~CSecureString();

				// container methods
			public:
				size_t AddChar(char* character);
				bool RemoveLastChar();
				void Reset();

				// decryption methods
			public:
				const char* Decrypt();
				void FlushUnencryptedData();
			};
		}
	}
}
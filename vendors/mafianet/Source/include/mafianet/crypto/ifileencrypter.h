/*
 *  Copyright (c) 2018-2019, SLikeSoft UG (haftungsbeschraenkt)
 *
 *  This source code is  licensed under the MIT-style license found in the license.txt
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
			class IFileEncrypter
			{
				// constructor / destructor
			protected:
				IFileEncrypter() = default;
			public:
				virtual ~IFileEncrypter() = default;

				// signing methods
			public:
				virtual const unsigned char* SignData(const unsigned char* data, const size_t dataLength) = 0;
				virtual const char* SignDataBase64(const unsigned char* data, const size_t dataLength) = 0;
				virtual bool VerifyData(const unsigned char *data, const size_t dataLength, const unsigned char *signature, const size_t signatureLength) = 0;
				virtual bool VerifyDataBase64(const unsigned char *data, const size_t dataLength, const char *signature, const size_t signatureLength) = 0;
			};
		}
	}
}
/*
 *  Copyright (c) 2018-2019, SLikeSoft UG (haftungsbeschraenkt)
 *
 *  This source code is  licensed under the MIT-style license found in the license.txt
 *  file in the root directory of this source tree.
 */
#include "mafianet/crypto/factory.h"

// includes for concrete classes
#include "mafianet/crypto/fileencrypter.h" // used for CFileEncrypter

namespace MafiaNet
{
	namespace Experimental
	{
		namespace Crypto
		{
			IFileEncrypter* Factory::ConstructFileEncrypter(const char *publicKey, size_t publicKeyLength)
			{
				return new CFileEncrypter(publicKey, publicKeyLength);
			}

			// #high - change interface --- use RakString?
			// #high - reconsider non-const CSecureString (for private key)
			IFileEncrypter* Factory::ConstructFileEncrypter(const char *publicKey, size_t publicKeyLength, const char *privateKey, size_t privateKeyLength, CSecureString& privateKeyPassword)
			{
				return new CFileEncrypter(publicKey, publicKeyLength, privateKey, privateKeyLength, privateKeyPassword);
			}
		}
	}
}
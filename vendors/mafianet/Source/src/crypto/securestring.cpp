/*
 *  Copyright (c) 2018-2019, SLikeSoft UG (haftungsbeschraenkt)
 *
 *  This source code is  licensed under the MIT-style license found in the license.txt
 *  file in the root directory of this source tree.
 */
#include "mafianet/crypto/securestring.h"

// #med - review the include order - defines.h defines SLNET_VERIFY but doesn't enforce including assert.h which it honestly should
#include "mafianet/crypto/cryptomanager.h" // used for CCryptoManager
#include "mafianet/assert.h"               // used for assert() (via SLNET_VERIFY)
#include "mafianet/memoryoverride.h"       // used for OP_NEW_ARRAY

#include <cstring> // used for std::memcpy
#include <limits>  // used for std::numeric_limits

namespace MafiaNet
{
	namespace Experimental
	{
		namespace Crypto
		{
			// #high - review UTF-8 mode handling --- quick and dirty addition atm - should use distinct UTF8 char-class?
			CSecureString::CSecureString(const size_t maxBufferSize, const bool utf8Mode) :
				m_UTF8Mode(utf8Mode),
				m_wasFlushed(false),
				m_numBufferSize(maxBufferSize),
				m_numBufferUsed(0),
				m_numEncryptedBufferUsed(0)
			{
				// #high - add proper handling for char sizes ~= 1 byte!

				// #med - add missing size_t overflow check if maxBufferSize == size_t::max()
				// #high - raise exception upon failure to retrieve the proper size
				// note: we don't encrypt the trailing null-terminator -> only requiring maxBufferSize amount of data here
				m_EncryptedBufferSize = maxBufferSize;
				SLNET_VERIFY(CCryptoManager::GetRequiredEncryptionBufferSize(m_EncryptedBufferSize));

				// note: we must set the unencrypted buffer size to the size required for the encrypted buffer (which is ensured to be >= maxBufferSize) since the
				//       CCryptoManager::DecryptSessionData() requires the unencrypted buffer size to be at least the size of the encrypted buffer (in order to prevent potential buffer overruns)
				m_UnencryptedBufferSize = m_EncryptedBufferSize + 1; // +1 for trailing '\0'-char (which is not part of the encrypted data)

				// #high - raise exception upon failure to allocate
				m_EncryptedMemory = static_cast<unsigned char*>(CCryptoManager::AllocateSecureMemory(m_EncryptedBufferSize));
				// note: also keep the unencrypted buffer in the secure memory space, so any memory specific security features (f.e. privilege or enclave
				//       restrictions) also apply for the unencrypted data
				m_UnencryptedBuffer = static_cast<char*>(CCryptoManager::AllocateSecureMemory(m_UnencryptedBufferSize));
			}

			CSecureString::~CSecureString()
			{
				// make sure that no data is leaked which could hint to the content of the secure string (i.e. the used size) after the object
				// was destroyed
				CCryptoManager::SecureClearMemory(&m_numBufferUsed, sizeof(size_t));

				CCryptoManager::FreeSecureMemory(m_EncryptedMemory, m_EncryptedBufferSize);
				m_EncryptedMemory = nullptr;

				CCryptoManager::FreeSecureMemory(m_UnencryptedBuffer, m_UnencryptedBufferSize);
				m_UnencryptedBuffer = nullptr;
			}

			size_t CSecureString::AddChar(char* character)
			{
				// #med skip or error out if '\0'? since this would just be pointless as the trailing null-terminator is written implicitly upon decryption...
				if (character == nullptr) {
					// #high - add error output
					return 0;
				}

				size_t charSize = 1;
				if (m_UTF8Mode) {
					// calculate char size
					if (static_cast<unsigned char>(character[0]) >= 0xF0) {
						charSize = 4;
					}
					else if(static_cast<unsigned char>(character[0]) >= 0xE0) {
						charSize = 3;
					}
					else if (static_cast<unsigned char>(character[0]) >= 0xC0) {
						charSize = 2;
					}
					else if (static_cast<unsigned char>(character[0]) >= 0x80) {
						// invalid encoding
						// #high - add error output
						// note: not nulling, since obviously the input data is wrong (not a UTF8-char)
						return 0;
					}
					// else single byte char

					// validate the 10-prefixes for bytes 2ff.
					for (size_t i = 1; i < charSize; ++i) {
						if (static_cast<unsigned char>(character[i]) < 0x80) {
							// #high - add error output
							// note: not nulling, since obviously the input data is wrong (not a UTF8-char)
							return 0;
						}
					}
				}

				if (m_numBufferUsed + charSize > m_numBufferSize) {
					// out of memory / ensure source data was cleared regardless
					// #high - add error output
					CCryptoManager::SecureClearMemory(character, charSize);
					return 0;
				}

				// decrypt, add, and re-encrypt the data
				// note: do not decrypting the memory if it wasn't encrypted yet
				if (m_numBufferUsed > 0) {
					RakAssert(m_numEncryptedBufferUsed > 0);
					size_t bufferSize = (m_UnencryptedBufferSize - 1);
					// #high - add error output
					if (!CCryptoManager::DecryptSessionData(m_EncryptedMemory, m_numEncryptedBufferUsed, reinterpret_cast<unsigned char*>(m_UnencryptedBuffer), bufferSize)) {
						// error decrypting the encrypted memory / ensure source data was cleared regardless
						CCryptoManager::SecureClearMemory(character, charSize);
						return 0;
					}
				}
				memcpy(m_UnencryptedBuffer + m_numBufferUsed, character, charSize);
				m_numBufferUsed += charSize;

				// clear the source data
				CCryptoManager::SecureClearMemory(character, charSize);

				// #high - add error handling
				size_t bufferSize = m_EncryptedBufferSize;
				// note: we always encrypt the entire buffer rather than just the used portion of it, so to not leak any information about the encrypted string (i.e. its length)
				// note: correct to use m_numBufferSize here which is the actual max data-length for the secure string
				const bool success = CCryptoManager::EncryptSessionData(reinterpret_cast<unsigned char*>(m_UnencryptedBuffer), m_numBufferSize, m_EncryptedMemory, bufferSize);
				if (success) {
					m_numEncryptedBufferUsed = bufferSize;
				}

				// clear the unencrypted buffer after it got reencrypted (to be safe, clear the full buffer, not just the m_numBufferSize portion / this also prevents leaking the
				// null-terminator in the buffer)
				CCryptoManager::SecureClearMemory(m_UnencryptedBuffer, m_UnencryptedBufferSize);

				return success ? charSize : 0;
			}

			bool CSecureString::RemoveLastChar()
			{
				if (m_numBufferUsed == 0) {
					return false; // empty buffer - nothing to remove
				}

				size_t numCharsToRemove = 0;
				if (m_UTF8Mode && (m_numBufferUsed >= 1)) {
					// note: if we are in UTF8-mode and only have a single encoded UTF8-byte left to remove, there's no need to decrypt and check the string
					//       adding a check here would be redundant with the check we do in AddChar(), which already ensures that there are no invalid UTF-8 encoded strings in the encrypted secure
					//       string buffer

					size_t bufferSize = (m_UnencryptedBufferSize - 1);
					if (!CCryptoManager::DecryptSessionData(m_EncryptedMemory, m_numEncryptedBufferUsed, reinterpret_cast<unsigned char*>(m_UnencryptedBuffer), bufferSize)) {
						// error decrypting the encrypted memory
						return false;
					}

					// iterate backwards over the UTF-8 encoded chars, to find the starting char (which will have a different encoding than 10xxxxxx)
					while ((static_cast<unsigned char>(m_UnencryptedBuffer[m_numBufferUsed - numCharsToRemove - 1]) & 0xC0) == 0x80) {
						++numCharsToRemove;
						RakAssert(m_numBufferUsed >= numCharsToRemove);
					}

					// clear the unencrypted buffer after it got reencrypted
					CCryptoManager::SecureClearMemory(m_UnencryptedBuffer, m_UnencryptedBufferSize);
				}
				++numCharsToRemove; // either remove a single char (in ASCII mode) or the first character before the continuing chars which were removed in the while-loop above
				RakAssert(numCharsToRemove <= 4);
				RakAssert(m_numBufferUsed >= numCharsToRemove);

				// note: no need to remove the character from the encrypted string - just act as if it was removed and clear the data the next time we decrypt the data
				m_numBufferUsed -= numCharsToRemove;
				// #high - add this handling
				//m_numCharsToClear += numCharsToClear;
				return true;
			}

			void CSecureString::Reset()
			{
				// resetting the memory buffer also explicitly resets any not-yet flushed unencrypted buffer
				FlushUnencryptedData();

				// also clear the encrypted data so to not leave behind any residues of the old data (even if only in encrypted form)
				CCryptoManager::SecureClearMemory(m_EncryptedMemory, m_EncryptedBufferSize);

				m_numBufferUsed = 0;
				m_numEncryptedBufferUsed = 0;
			}

			const char* CSecureString::Decrypt()
			{
				// #high - add mutex to prevent threading issues

				if (m_numBufferUsed == 0) {
					return ""; // no encrypted data (prevent decrypting non-encrypted data - i.e. if never any data was added)
				}

				// flushing the data here to safeguard against the user having forgotten to flush existing unencrypted data of some old encrypted data
				// (which then would remain in memory if the new data's length is smaller than the old ones)
				// note: since we flush the data, we don't need to write a trailing null-terminator lateron (i.e. entire memory is zeroed here)
				FlushUnencryptedData();

				size_t bufferSize = (m_UnencryptedBufferSize - 1);
				if (!CCryptoManager::DecryptSessionData(m_EncryptedMemory, m_numEncryptedBufferUsed, reinterpret_cast<unsigned char*>(m_UnencryptedBuffer), bufferSize)) {
					// error decrypting the encrypted memory
					// #high - add error handling
					return "";
				}

				// write trailing null-terminator (which is not part of the encrypted data)
				m_UnencryptedBuffer[m_numBufferUsed] = '\0';
				m_wasFlushed = false;
				return m_UnencryptedBuffer;
			}

			void CSecureString::FlushUnencryptedData()
			{
				if (!m_wasFlushed) {
					CCryptoManager::SecureClearMemory(m_UnencryptedBuffer, m_UnencryptedBufferSize);
					m_wasFlushed = true;
				}
			}
		}
	}
}
/*
 * Architecture selector for vendored OpenSSL configuration.
 *
 * OpenSSL generates this header with ABI-specific values. Keep the generated
 * variants next to this wrapper and include the one matching the target.
 */

#ifndef OPENSSL_CONFIGURATION_SELECTOR_H
# define OPENSSL_CONFIGURATION_SELECTOR_H

# if defined(_WIN32) && !defined(_WIN64)
#  include <openssl/configuration_x86.h>
# else
#  include <openssl/configuration_x64.h>
# endif

#endif /* OPENSSL_CONFIGURATION_SELECTOR_H */

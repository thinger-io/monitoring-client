#pragma once

// base64
#include <string>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <cctype>

// hash
#include <openssl/sha.h>
#include <openssl/hmac.h>

namespace Crypto {

    std::string to_hex(std::string string) {

        unsigned int len = string.length();
        //unsigned char hash[len];
        unsigned char* hash = (unsigned char*)string.c_str();

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < len; i++) {
            ss << std::hex << std::setw(2)  << (unsigned int)hash[i];
            //ss << std::hex << std::setw(2)  << string.at(i);
        }

        return ss.str();
    }

    namespace base64 {
        /*
         * Code from https://stackoverflow.com/a/5291537/10268962 without modification
         * Protected by CC BY-SA 3.0 (https://creativecommons.org/licenses/by-sa/3.0/)
         */

        const char b64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        const char reverse_table[128] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
        };

        ::std::string encode(const ::std::string &bindata)
        {
            using ::std::string;
            using ::std::numeric_limits;

            if (bindata.size() > (numeric_limits<string::size_type>::max() / 4u) * 3u) {
                throw ::std::length_error("Converting too large a string to base64.");
            }

            const ::std::size_t binlen = bindata.size();
            // Use = signs so the end is properly padded.
            string retval((((binlen + 2) / 3) * 4), '=');
            ::std::size_t outpos = 0;
            int bits_collected = 0;
            unsigned int accumulator = 0;
            const string::const_iterator binend = bindata.end();

            for (string::const_iterator i = bindata.begin(); i != binend; ++i) {
                accumulator = (accumulator << 8) | (*i & 0xffu);
                bits_collected += 8;
                while (bits_collected >= 6) {
                    bits_collected -= 6;
                    retval[outpos++] = b64_table[(accumulator >> bits_collected) & 0x3fu];
                }
            }
            if (bits_collected > 0) { // Any trailing bits that are missing.
                assert(bits_collected < 6);
                accumulator <<= 6 - bits_collected;
                retval[outpos++] = b64_table[accumulator & 0x3fu];
            }
            assert(outpos >= (retval.size() - 2));
            assert(outpos <= retval.size());
            return retval;
        }

        static ::std::string decode(const ::std::string &ascdata)
        {
            using ::std::string;
            string retval;
            const string::const_iterator last = ascdata.end();
            int bits_collected = 0;
            unsigned int accumulator = 0;

            for (string::const_iterator i = ascdata.begin(); i != last; ++i) {
                const int c = *i;
                if (::std::isspace(c) || c == '=') {
                    // Skip whitespace and padding. Be liberal in what you accept.
                    continue;
                }
                if ((c > 127) || (c < 0) || (reverse_table[c] > 63)) {
                    throw ::std::invalid_argument("This contains characters not legal in a base64 encoded string.");
                }
                accumulator = (accumulator << 6) | reverse_table[c];
                bits_collected += 6;
                if (bits_collected >= 8) {
                    bits_collected -= 8;
                    retval += static_cast<char>((accumulator >> bits_collected) & 0xffu);
                }
            }
            return retval;
        }
    };

    namespace hash {

        std::string hmac_sha1(std::string key, std::string msg) {

            unsigned char hash[32];

            HMAC_CTX *hmac = HMAC_CTX_new();
            HMAC_Init_ex(hmac, &key[0], key.length(), EVP_sha1(), NULL);
            HMAC_Update(hmac, (unsigned char*)&msg[0], msg.length());
            unsigned int len = 32;
            HMAC_Final(hmac, hash, &len);
            HMAC_CTX_free(hmac);

            /* HEX
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (int i = 0; i < len; i++) {
                ss << std::hex << std::setw(2)  << (unsigned int)hash[i];
            }
            */

            std::stringstream ss;
            ss << std::setfill('0');
            for (int i = 0; i < len; i++) {
                ss  << hash[i];
            }

            //return (base64_encode(ss.str()));
            return ss.str();

        }

        std::string hmac_sha256(std::string key, std::string msg) {

            unsigned char hash[64];

            HMAC_CTX *hmac = HMAC_CTX_new();
            HMAC_Init_ex(hmac, &key[0], key.length(), EVP_sha256(), NULL);
            HMAC_Update(hmac, (unsigned char*)&msg[0], msg.length());
            unsigned int len = 64;
            HMAC_Final(hmac, hash, &len);
            HMAC_CTX_free(hmac);

            // HEX
            /*std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (int i = 0; i < len; i++) {
                ss << std::hex << std::setw(2)  << (unsigned int)hash[i];
            }
            */

            std::stringstream ss;
            ss << std::setfill('0');
            for (int i = 0; i < len; i++) {
                ss  << hash[i];
            }

            return ss.str();

        }

        std::string sha256(const char* str, const size_t length) {
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, str, length);
            SHA256_Final(hash, &sha256);
            std::stringstream ss;
            for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            return ss.str();
        }

        std::string sha256(const std::string str) {
            /*unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, str.c_str(), str.size());
            SHA256_Final(hash, &sha256);
            std::stringstream ss;
            for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            return ss.str();
            */
            return sha256(str.c_str(), str.length());
        }
    };

};

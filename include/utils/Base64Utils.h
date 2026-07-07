#ifndef BASE64_UTILS_H
#define BASE64_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace utils {

// Decode a Base64-encoded string to raw bytes.
// Returns empty vector on invalid input.
std::vector<uint8_t> base64Decode(const std::string& input);

// Check if a string appears to be valid Base64 (only A-Za-z0-9+/= chars, length multiple of 4)
bool looksLikeBase64(const std::string& input);

} // namespace utils

#endif // BASE64_UTILS_H

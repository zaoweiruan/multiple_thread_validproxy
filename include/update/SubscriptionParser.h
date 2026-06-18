#ifndef SUBSCRIPTION_PARSER_H
#define SUBSCRIPTION_PARSER_H

#include <string>
#include <vector>
#include "Profileitem.h"

namespace update {

class SubscriptionParser {
public:
    SubscriptionParser() = default;
    
    std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid);

private:
    std::string decodeBase64(const std::string& input);
    std::string urlDecode(const std::string& input);
    std::pair<std::string, std::string> parseAddressPort(const std::string& addrPart);
};

} // namespace update

#endif // SUBSCRIPTION_PARSER_H
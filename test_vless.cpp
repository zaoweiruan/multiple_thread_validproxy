#include <iostream>
#include "../include/ShareLink.h"

int main() {
    // 模拟从数据库读取的 VLESS 记录
    std::string result = share::ShareLink::toShareUri(
        "5",  // VLESS
        "135.84.74.152",  // address
        "443",  // port
        "6202b230-417c-4d8e-b624-0f71afa9c75d",  // id
        "none",  // security
        "ws",  // network
        "",  // flow
        "sni.111000.dynv6.net",  // sni
        "",  // alpn
        "chrome",  // fingerprint
        "true",  // allowinsecure - 数据库存为 "true"
        "/",  // path (should be filtered)
        "sni.111000.dynv6.net",  // requesthost
        "",  // headertype - empty (should omit headerType)
        "tls",  // streamsecurity
        "RELAY🚑",  // remarks
        "",  // ech
        "",  // pbk
        ""   // sid
    );
    std::cout << result << std::endl;
    return 0;
}

#ifndef DB_PROFILEITEM_H
#define DB_PROFILEITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Profileitem {
  std::string indexid;  // IndexId
  std::string configtype;  // ConfigType
  std::string configversion;  // ConfigVersion
  std::string address;  // Address
  std::string port;  // Port
  std::string id;  // Id
  std::string alterid;  // AlterId
  std::string security;  // Security
  std::string network;  // Network
  std::string remarks;  // Remarks
  std::string headertype;  // HeaderType
  std::string requesthost;  // RequestHost
  std::string path;  // Path
  std::string streamsecurity;  // StreamSecurity
  std::string allowinsecure;  // AllowInsecure
  std::string subid;  // Subid
  std::string issub;  // IsSub
  std::string flow;  // Flow
  std::string sni;  // Sni
  std::string alpn;  // Alpn
  std::string coretype;  // CoreType
  std::string presocksport;  // PreSocksPort
  std::string fingerprint;  // Fingerprint
  std::string displaylog;  // DisplayLog
  std::string publickey;  // PublicKey
  std::string shortid;  // ShortId
  std::string spiderx;  // SpiderX
  std::string extra;  // Extra
  std::string ports;  // Ports
  std::string mldsa65verify;  // Mldsa65Verify
  std::string muxenabled;  // MuxEnabled
  std::string cert;  // Cert
  // gRPC
  int grpcMultiMode = 0;
  // KCP
  int kcpMtu = 1350;
  int kcpTti = 20;
  int kcpUplink = 12;
  int kcpDownlink = 20;
  int kcpCongestion = 0;
  int kcpReadBufferSize = 2;
  int kcpWriteBufferSize = 2;

  std::string kcpHeaderType = "none";
  int muxEnabled;
  Profileitem() = default;
  
  void checkRequired() const {
    if (address.empty()) {
      throw std::runtime_error("Address is empty");
    }
    if (port.empty() || std::stoi(port) <= 0 || std::stoi(port) > 65535) {
      throw std::runtime_error("Invalid port: " + port);
    }
    if (id.empty()) {
      throw std::runtime_error("ID/Password is empty");
    }
    if (configtype.empty()) {
      throw std::runtime_error("ConfigType is empty");
    }
    if (streamsecurity == "reality") {
      if (publickey.empty()) {
        throw std::runtime_error("REALITY requires publicKey");
      }
      if (sni.empty()) {
        throw std::runtime_error("REALITY requires sni");
      }
    }
  }

  static Profileitem fromStmt(sqlite3_stmt* stmt) {
    Profileitem obj;
    const char* text;
    // 实际表字段顺序: IndexId, ConfigType, ConfigVersion, Address, Port, Ports, Id, AlterId, Security, Network, Remarks, HeaderType, RequestHost, Path, StreamSecurity, AllowInsecure, Subid, IsSub, Flow, Sni, Alpn, CoreType, PreSocksPort, Fingerprint, DisplayLog, PublicKey, ShortId, SpiderX, Mldsa65Verify, Extra, MuxEnabled, Cert, CertSha, EchConfigList, EchForceQuery
    // 索引:           0          1          2              3       4      5      6     7        8        9       10        11          12          13    14              15            16      17     18    19    20    21        22           23          24         25       26       27             28           29      30         31       32          33            34
    // IndexId
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // ConfigType
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.configtype = text ? text : "";
    // ConfigVersion
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.configversion = text ? text : "";
    // Address
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.address = text ? text : "";
    // Port
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.port = text ? text : "";
    // Ports
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.ports = text ? text : "";
    // Id
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.id = text ? text : "";
    // AlterId
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.alterid = text ? text : "";
    // Security
    text = (const char*)sqlite3_column_text(stmt, 8);
    obj.security = text ? text : "";
    // Network
    text = (const char*)sqlite3_column_text(stmt, 9);
    obj.network = text ? text : "";
    // Remarks
    text = (const char*)sqlite3_column_text(stmt, 10);
    obj.remarks = text ? text : "";
    // HeaderType
    text = (const char*)sqlite3_column_text(stmt, 11);
    obj.headertype = text ? text : "";
    // RequestHost
    text = (const char*)sqlite3_column_text(stmt, 12);
    obj.requesthost = text ? text : "";
    // Path
    text = (const char*)sqlite3_column_text(stmt, 13);
    obj.path = text ? text : "";
    // StreamSecurity
    text = (const char*)sqlite3_column_text(stmt, 14);
    obj.streamsecurity = text ? text : "";
    // AllowInsecure
    text = (const char*)sqlite3_column_text(stmt, 15);
    obj.allowinsecure = text ? text : "";
    // Subid
    text = (const char*)sqlite3_column_text(stmt, 16);
    obj.subid = text ? text : "";
    // IsSub
    text = (const char*)sqlite3_column_text(stmt, 17);
    obj.issub = text ? text : "";
    // Flow
    text = (const char*)sqlite3_column_text(stmt, 18);
    obj.flow = text ? text : "";
    // Sni
    text = (const char*)sqlite3_column_text(stmt, 19);
    obj.sni = text ? text : "";
    // Alpn
    text = (const char*)sqlite3_column_text(stmt, 20);
    obj.alpn = text ? text : "";
    // CoreType
    text = (const char*)sqlite3_column_text(stmt, 21);
    obj.coretype = text ? text : "";
    // PreSocksPort
    text = (const char*)sqlite3_column_text(stmt, 22);
    obj.presocksport = text ? text : "";
    // Fingerprint
    text = (const char*)sqlite3_column_text(stmt, 23);
    obj.fingerprint = text ? text : "";
    // DisplayLog
    text = (const char*)sqlite3_column_text(stmt, 24);
    obj.displaylog = text ? text : "";
    // PublicKey
    text = (const char*)sqlite3_column_text(stmt, 25);
    obj.publickey = text ? text : "";
    // ShortId
    text = (const char*)sqlite3_column_text(stmt, 26);
    obj.shortid = text ? text : "";
    // SpiderX
    text = (const char*)sqlite3_column_text(stmt, 27);
    obj.spiderx = text ? text : "";
    // Mldsa65Verify
    text = (const char*)sqlite3_column_text(stmt, 28);
    obj.mldsa65verify = text ? text : "";
    // Extra
    text = (const char*)sqlite3_column_text(stmt, 29);
    obj.extra = text ? text : "";
    // MuxEnabled
    text = (const char*)sqlite3_column_text(stmt, 30);
    obj.muxenabled = text ? text : "";
    // Cert
    text = (const char*)sqlite3_column_text(stmt, 31);
    obj.cert = text ? text : "";
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"IndexId\": ";
    oss << indexid;  // 简化输出
    oss << ", ";
    oss << "\"ConfigType\": ";
    oss << configtype;  // 简化输出
    oss << ", ";
    oss << "\"ConfigVersion\": ";
    oss << configversion;  // 简化输出
    oss << ", ";
    oss << "\"Address\": ";
    oss << address;  // 简化输出
    oss << ", ";
    oss << "\"Port\": ";
    oss << port;  // 简化输出
    oss << ", ";
    oss << "\"Id\": ";
    oss << id;  // 简化输出
    oss << ", ";
    oss << "\"AlterId\": ";
    oss << alterid;  // 简化输出
    oss << ", ";
    oss << "\"Security\": ";
    oss << security;  // 简化输出
    oss << ", ";
    oss << "\"Network\": ";
    oss << network;  // 简化输出
    oss << ", ";
    oss << "\"Remarks\": ";
    oss << remarks;  // 简化输出
    oss << ", ";
    oss << "\"HeaderType\": ";
    oss << headertype;  // 简化输出
    oss << ", ";
    oss << "\"RequestHost\": ";
    oss << requesthost;  // 简化输出
    oss << ", ";
    oss << "\"Path\": ";
    oss << path;  // 简化输出
    oss << ", ";
    oss << "\"StreamSecurity\": ";
    oss << streamsecurity;  // 简化输出
    oss << ", ";
    oss << "\"AllowInsecure\": ";
    oss << allowinsecure;  // 简化输出
    oss << ", ";
    oss << "\"Subid\": ";
    oss << subid;  // 简化输出
    oss << ", ";
    oss << "\"IsSub\": ";
    oss << issub;  // 简化输出
    oss << ", ";
    oss << "\"Flow\": ";
    oss << flow;  // 简化输出
    oss << ", ";
    oss << "\"Sni\": ";
    oss << sni;  // 简化输出
    oss << ", ";
    oss << "\"Alpn\": ";
    oss << alpn;  // 简化输出
    oss << ", ";
    oss << "\"CoreType\": ";
    oss << coretype;  // 简化输出
    oss << ", ";
    oss << "\"PreSocksPort\": ";
    oss << presocksport;  // 简化输出
    oss << ", ";
    oss << "\"Fingerprint\": ";
    oss << fingerprint;  // 简化输出
    oss << ", ";
    oss << "\"DisplayLog\": ";
    oss << displaylog;  // 简化输出
    oss << ", ";
    oss << "\"PublicKey\": ";
    oss << publickey;  // 简化输出
    oss << ", ";
    oss << "\"ShortId\": ";
    oss << shortid;  // 简化输出
    oss << ", ";
    oss << "\"SpiderX\": ";
    oss << spiderx;  // 简化输出
    oss << ", ";
    oss << "\"Extra\": ";
    oss << extra;  // 简化输出
    oss << ", ";
    oss << "\"Ports\": ";
    oss << ports;  // 简化输出
    oss << ", ";
    oss << "\"Mldsa65Verify\": ";
    oss << mldsa65verify;  // 简化输出
    oss << ", ";
    oss << "\"MuxEnabled\": ";
    oss << muxenabled;  // 简化输出
    oss << ", ";
    oss << "\"Cert\": ";
    oss << cert;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class ProfileitemDAO {
private:
  sqlite3* db_;

public:
  explicit ProfileitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Profileitem> getAll(const std::string& sql = "SELECT * FROM ProfileItem;") {
    std::vector<Profileitem> result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Profileitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_PROFILEITEM_H

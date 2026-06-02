#ifndef DB_PROFILEITEM_H
#define DB_PROFILEITEM_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <random>
#include <array>
#include "Logger.h"

namespace db {
namespace models {

struct Profileitem {
  std::string indexid;         // 0: IndexId
  std::string configtype;       // 1: ConfigType
  std::string configversion;    // 2: ConfigVersion
  std::string address;          // 3: Address
  std::string port;             // 4: Port
  std::string ports;            // 5: Ports
  std::string id;               // 6: Id
  std::string alterid;         // 7: AlterId
  std::string security;        // 8: Security
  std::string network;         // 9: Network
  std::string remarks;         // 10: Remarks
  std::string headertype;      // 11: HeaderType
  std::string requesthost;     // 12: RequestHost
  std::string path;             // 13: Path
  std::string streamsecurity;  // 14: StreamSecurity
  std::string allowinsecure;   // 15: AllowInsecure
  std::string subid;           // 16: Subid
  std::string issub;            // 17: IsSub
  std::string flow;             // 18: Flow
  std::string sni;              // 19: Sni
  std::string alpn;             // 20: Alpn
  std::string coretype;        // 21: CoreType
  std::string presocksport;    // 22: PreSocksPort
  std::string fingerprint;     // 23: Fingerprint
  std::string displaylog;      // 24: DisplayLog
  std::string publickey;       // 25: PublicKey
  std::string shortid;         // 26: ShortId
  std::string spiderx;         // 27: SpiderX
  std::string mldsa65verify;   // 28: Mldsa65Verify
  std::string extra;            // 29: Extra
  std::string muxenabled;      // 30: MuxEnabled
  std::string cert;             // 31: Cert
  std::string certsha;         // 32: CertSha
  std::string echconfiglist;   // 33: EchConfigList
  std::string echforcequery;   // 34: EchForceQuery
  
  // 非数据库字段
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
  int muxEnabled;  // KCP 配置复用
  
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
    // SQL字段顺序（必须和结构体字段顺序一致）: IndexId(0), ConfigType(1), ConfigVersion(2), Address(3), Port(4), Ports(5), Id(6), AlterId(7), Security(8), Network(9), Remarks(10), HeaderType(11), RequestHost(12), Path(13), StreamSecurity(14), AllowInsecure(15), Subid(16), IsSub(17), Flow(18), Sni(19), Alpn(20), CoreType(21), PreSocksPort(22), Fingerprint(23), DisplayLog(24), PublicKey(25), ShortId(26), SpiderX(27), Mldsa65Verify(28), Extra(29), MuxEnabled(30), Cert(31), CertSha(32), EchConfigList(33), EchForceQuery(34)
    
    // IndexId(0)
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // ConfigType(1)
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.configtype = text ? text : "";
    // ConfigVersion(2)
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.configversion = text ? text : "";
    // Address(3)
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.address = text ? text : "";
    // Port(4)
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.port = text ? text : "";
    // Ports(5)
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.ports = text ? text : "";
    // Id(6)
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.id = text ? text : "";
    // AlterId(7)
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.alterid = text ? text : "";
    // Security(8)
    text = (const char*)sqlite3_column_text(stmt, 8);
    obj.security = text ? text : "";
    // Network(9)
    text = (const char*)sqlite3_column_text(stmt, 9);
    obj.network = text ? text : "";
    // Remarks(10)
    text = (const char*)sqlite3_column_text(stmt, 10);
    obj.remarks = text ? text : "";
    // HeaderType(11)
    text = (const char*)sqlite3_column_text(stmt, 11);
    obj.headertype = text ? text : "";
    // RequestHost(12)
    text = (const char*)sqlite3_column_text(stmt, 12);
    obj.requesthost = text ? text : "";
    // Path(13)
    text = (const char*)sqlite3_column_text(stmt, 13);
    obj.path = text ? text : "";
    // StreamSecurity(14)
    text = (const char*)sqlite3_column_text(stmt, 14);
    obj.streamsecurity = text ? text : "";
    // AllowInsecure(15)
    text = (const char*)sqlite3_column_text(stmt, 15);
    obj.allowinsecure = text ? text : "";
    // Subid(16)
    text = (const char*)sqlite3_column_text(stmt, 16);
    obj.subid = text ? text : "";
    // IsSub(17)
    text = (const char*)sqlite3_column_text(stmt, 17);
    obj.issub = text ? text : "";
    // Flow(18)
    text = (const char*)sqlite3_column_text(stmt, 18);
    obj.flow = text ? text : "";
    // Sni(19)
    text = (const char*)sqlite3_column_text(stmt, 19);
    obj.sni = text ? text : "";
    // Alpn(20)
    text = (const char*)sqlite3_column_text(stmt, 20);
    obj.alpn = text ? text : "";
    // CoreType(21)
    text = (const char*)sqlite3_column_text(stmt, 21);
    obj.coretype = text ? text : "";
    // PreSocksPort(22)
    text = (const char*)sqlite3_column_text(stmt, 22);
    obj.presocksport = text ? text : "";
    // Fingerprint(23)
    text = (const char*)sqlite3_column_text(stmt, 23);
    obj.fingerprint = text ? text : "";
    // DisplayLog(24)
    text = (const char*)sqlite3_column_text(stmt, 24);
    obj.displaylog = text ? text : "";
    // PublicKey(25)
    text = (const char*)sqlite3_column_text(stmt, 25);
    obj.publickey = text ? text : "";
    // ShortId(26)
    text = (const char*)sqlite3_column_text(stmt, 26);
    obj.shortid = text ? text : "";
    // SpiderX(27)
    text = (const char*)sqlite3_column_text(stmt, 27);
    obj.spiderx = text ? text : "";
    // Mldsa65Verify(28)
    text = (const char*)sqlite3_column_text(stmt, 28);
    obj.mldsa65verify = text ? text : "";
    // Extra(29)
    text = (const char*)sqlite3_column_text(stmt, 29);
    obj.extra = text ? text : "";
    // MuxEnabled(30)
    text = (const char*)sqlite3_column_text(stmt, 30);
    obj.muxenabled = text ? text : "";
    // Cert(31)
    text = (const char*)sqlite3_column_text(stmt, 31);
    obj.cert = text ? text : "";
    // CertSha(32)
    text = (const char*)sqlite3_column_text(stmt, 32);
    obj.certsha = text ? text : "";
    // EchConfigList(33)
    text = (const char*)sqlite3_column_text(stmt, 33);
    obj.echconfiglist = text ? text : "";
    // EchForceQuery(34)
    text = (const char*)sqlite3_column_text(stmt, 34);
    obj.echforcequery = text ? text : "";
    
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
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Profileitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
  
  /// Efficiently count profiles per subscription using a single GROUP BY query.
  /// Returns a map of subid → profile count, avoiding N+1 full-table scans.
  std::unordered_map<std::string, int> countBySubId() {
    std::unordered_map<std::string, int> result;
    const char* sql = "SELECT SubId, COUNT(*) FROM ProfileItem GROUP BY SubId;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* subId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      int count = sqlite3_column_int(stmt, 1);
      if (subId) {
        result[subId] = count;
      }
    }
    sqlite3_finalize(stmt);
    return result;
  }

  std::optional<Profileitem> getByIndexId(const std::string& indexId) {
    std::string sql = "SELECT * FROM ProfileItem WHERE IndexId = '" + indexId + "';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      return std::nullopt;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      Profileitem item = Profileitem::fromStmt(stmt);
      sqlite3_finalize(stmt);
      return item;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
  }

bool deleteBySubId(const std::string& subId) {
     std::string sql = "DELETE FROM ProfileItem WHERE Subid = '" + escape(subId) + "';";
     char* errMsg = nullptr;
     int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
     if (rc != SQLITE_OK) {
       Logger::write("Delete proxies by subId error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
       sqlite3_free(errMsg);
       return false;
     }
     return sqlite3_changes(db_) > 0;
   }
};

} // namespace models
} // namespace db

#endif // DB_PROFILEITEM_H

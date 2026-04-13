// ProfileSerializer.cpp
#include "ProfileSerializer.h"

boost::json::object ToJson(const ProfileItem& item) {
    boost::json::object obj;
    obj["indexId"] = item.IndexId;
    obj["configType"] = static_cast<int>(item.ConfigType);
    obj["coreType"] = static_cast<int>(item.CoreType);
    obj["configVersion"] = item.ConfigVersion;
    obj["subid"] = item.Subid;
    obj["remarks"] = item.Remarks;
    obj["address"] = item.Address;
    obj["port"] = item.Port;
    obj["password"] = item.Password;
    obj["network"] = item.Network;
    obj["streamSecurity"] = item.StreamSecurity;
    obj["sni"] = item.Sni;
    obj["fingerprint"] = item.Fingerprint;
    obj["publicKey"] = item.PublicKey;
    obj["shortId"] = item.ShortId;
    obj["path"] = item.Path;
    obj["protoExtra"] = item.ProtoExtra;
    obj["enabled"] = item.Enabled;
    obj["sort"] = item.Sort;
    return obj;
}

ProfileItem FromJson(const boost::json::value& jv) {
    auto& obj = jv.as_object();
    ProfileItem item;
    item.IndexId = obj["indexId"].as_string().c_str();
    item.ConfigType = static_cast<EConfigType>(obj["configType"].to_number<int>());
    item.CoreType = static_cast<ECoreType>(obj["coreType"].to_number<int>());
    item.ConfigVersion = obj["configVersion"].to_number<int>();
    item.Remarks = obj["remarks"].as_string().c_str();
    item.Address = obj["address"].as_string().c_str();
    item.Port = obj["port"].to_number<int>();
    item.Password = obj["password"].as_string().c_str();
    // ... 其他字段类似处理
    return item;
}
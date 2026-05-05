CREATE TABLE ProfileItem (
    IndexId TEXT PRIMARY KEY,
    ConfigType TEXT,
    ConfigVersion TEXT,
    Address TEXT,
    Port TEXT,
    Ports TEXT,
    Id TEXT,
    AlterId TEXT,
    Security TEXT,
    Network TEXT,
    Remarks TEXT,
    HeaderType TEXT,
    RequestHost TEXT,
    Path TEXT,
    StreamSecurity TEXT,
    AllowInsecure TEXT,
    Subid TEXT,
    IsSub TEXT,
    Flow TEXT,
    Sni TEXT,
    Alpn TEXT,
    CoreType TEXT,
    PreSocksPort TEXT,
    Fingerprint TEXT,
    DisplayLog TEXT,
    PublicKey TEXT,
    ShortId TEXT,
    SpiderX TEXT,
    Mldsa65Verify TEXT,
    Extra TEXT,
    MuxEnabled TEXT,
    Cert TEXT,
    CertSha TEXT,
    EchConfigList TEXT,
    EchForceQuery TEXT
);
INSERT INTO ProfileItem (IndexId, ConfigType, Address, Port, Id, Security, Network, Remarks, HeaderType, RequestHost, Path, StreamSecurity, AllowInsecure, Flow, Sni, Alpn, Fingerprint, PublicKey, ShortId, Subid)
VALUES ('test001', '5', '135.84.74.152', '443', '6202b230-417c-4d8e-b624-0f71afa9c75d', 'none', 'ws', 'RELAY🚑', '', 'sni.111000.dynv6.net', '/', 'tls', '1', '', 'sni.111000.dynv6.net', '', 'chrome', '', '', '5126987642659995691');

CREATE TABLE ProfileExItem (
    IndexId TEXT PRIMARY KEY,
    Delay TEXT,
    Speed TEXT,
    Sort TEXT,
    Message TEXT,
    consecutive_failures INTEGER DEFAULT 0,
    blacklisted INTEGER DEFAULT 0
);
INSERT INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message) VALUES ('test001', '100', '0', '0', 'NOT_TESTED');

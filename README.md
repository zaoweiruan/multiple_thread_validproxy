# 代理管理

## 安装

### 1.1 数据库维护
-  对v2rayN原生guinDB.db 执行scripts\sync_db_schema.sql,对齐表结构
### 1.2 配置
-  检查config.json、xray-config-template.json、singbox-config-template.json文件是否位于可执行文件同目录
-  配置xray、sing-box代理终端可执行文件路径
-  配置geoip.dat、geosite.dat文件的xray_location_asset路径
-  检查singbox-config-template.json中srss文件路径是否正确


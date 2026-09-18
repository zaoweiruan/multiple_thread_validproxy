分类重构方案：

├── 数据库
├── 监控悬浮窗             ← 重命名（原「监控代理进程」 ）：proxy_process_monitor.*
└── 健康度评估← 新增
		├── 独立代理评估    ← 新增（解决）：proxy.scoring_*_weight 三项
		├── 代理池评估      ← 拆分（解决）：standalone_pool.evaluate.* 六项	
├── 代理池配置      ← 拆分（解决）：standalone_pool.enabled/mode/socksPort/apiPort/balancerStrategy
├── 日志
├── Xray 全局 (workers / start_port / api_port)
├── 测试 (URL / timeout_ms)
├── 订阅
├── 去重
├── 通知
├── 自动任务
└── 代理后端选择           ← 新增：仅 use_singbox 切换器 + 后端路径字段（条件化可见，解决 P3）
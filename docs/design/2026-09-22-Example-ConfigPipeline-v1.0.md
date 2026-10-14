# ConfigPipeline 示例配置

本文档提供 6 个管线的示例配置，涵盖所有 10 种步骤类型。

## 步骤类型参考

| 步骤类型 | 中文名称 | 必需参数 | 可选参数 |
|----------|----------|----------|----------|
| `replace_variables` | 变量替换 | — | — |
| `add_outbound` | 添加出站 | `tag`, `source` | `protocol`, `settings` |
| `merge_routing` | 合并路由 | `inboundTag` | `outboundTag`, `balancerTag` |
| `add_inbound` | 添加入站 | `tag` | `protocol`, `port`, `settings` |
| `add_observatory` | 添加观测器 | `subjectSelector` | `probeURL`, `probeInterval` |
| `add_balancer` | 添加负载均衡 | `tag` | `selector`, `strategy`, `observation` |
| `add_stats` | 启用统计 | — | — |
| `add_policy` | 添加策略 | — | `levels`, `system` |
| `add_api` | 添加 API | `tag` | `listen`, `services` |
| `modify_field` | 修改字段 | `path`, `value` | — |

## 示例 1: test（测试模式）

```json
{
    "test": {
        "templateId": "xray_test_001",
        "steps": [
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10808
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "proxy"
                }
            }
        ]
    }
}
```

## 示例 2: test_singbox（SingBox 测试模式）

```json
{
    "test_singbox": {
        "templateId": "singbox_test_001",
        "steps": [
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10808
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "proxy"
                }
            }
        ]
    }
}
```

## 示例 3: standalone（独立代理模式）

```json
{
    "standalone": {
        "templateId": "xray_standalone_001",
        "steps": [
            {
                "type": "add_api",
                "params": {
                    "tag": "api",
                    "listen": "127.0.0.1:10085",
                    "services": ["HandlerService", "StatsService"]
                }
            },
            {
                "type": "add_stats",
                "params": {}
            },
            {
                "type": "add_policy",
                "params": {
                    "levels": {
                        "0": {
                            "statsUserUplink": true,
                            "statsUserDownlink": true,
                            "statsUserSession": true
                        }
                    },
                    "system": {
                        "statsInboundUplink": true,
                        "statsInboundDownlink": true,
                        "statsOutboundUplink": true,
                        "statsOutboundDownlink": true
                    }
                }
            },
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10809
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "proxy"
                }
            }
        ]
    }
}
```

## 示例 4: standalone_singbox（SingBox 独立代理模式）

```json
{
    "standalone_singbox": {
        "templateId": "singbox_standalone_001",
        "steps": [
            {
                "type": "add_api",
                "params": {
                    "tag": "api",
                    "listen": "127.0.0.1:10085",
                    "services": ["HandlerService", "StatsService"]
                }
            },
            {
                "type": "add_stats",
                "params": {}
            },
            {
                "type": "add_policy",
                "params": {
                    "levels": {
                        "0": {
                            "statsUserUplink": true,
                            "statsUserDownlink": true,
                            "statsUserSession": true
                        }
                    },
                    "system": {
                        "statsInboundUplink": true,
                        "statsInboundDownlink": true,
                        "statsOutboundUplink": true,
                        "statsOutboundDownlink": true
                    }
                }
            },
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10809
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "proxy"
                }
            }
        ]
    }
}
```

## 示例 5: pool（代理池模式）

```json
{
    "pool": {
        "templateId": "xray_pool_001",
        "steps": [
            {
                "type": "add_api",
                "params": {
                    "tag": "api",
                    "listen": "127.0.0.1:10085",
                    "services": ["HandlerService", "StatsService"]
                }
            },
            {
                "type": "add_stats",
                "params": {}
            },
            {
                "type": "add_policy",
                "params": {
                    "levels": {
                        "0": {
                            "statsUserUplink": true,
                            "statsUserDownlink": true,
                            "statsUserSession": true
                        }
                    },
                    "system": {
                        "statsInboundUplink": true,
                        "statsInboundDownlink": true,
                        "statsOutboundUplink": true,
                        "statsOutboundDownlink": true
                    }
                }
            },
            {
                "type": "add_observatory",
                "params": {
                    "subjectSelector": ["proxy"],
                    "probeURL": "https://www.google.com/generate_204",
                    "probeInterval": 5
                }
            },
            {
                "type": "add_balancer",
                "params": {
                    "tag": "balancer-out",
                    "selector": ["proxy"],
                    "strategy": "leastPing",
                    "observation": "observatory"
                }
            },
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10809
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "balancer-out"
                }
            }
        ]
    }
}
```

## 示例 6: pool_singbox（SingBox 代理池模式）

```json
{
    "pool_singbox": {
        "templateId": "singbox_pool_001",
        "steps": [
            {
                "type": "add_api",
                "params": {
                    "tag": "api",
                    "listen": "127.0.0.1:10085",
                    "services": ["HandlerService", "StatsService"]
                }
            },
            {
                "type": "add_stats",
                "params": {}
            },
            {
                "type": "add_policy",
                "params": {
                    "levels": {
                        "0": {
                            "statsUserUplink": true,
                            "statsUserDownlink": true,
                            "statsUserSession": true
                        }
                    },
                    "system": {
                        "statsInboundUplink": true,
                        "statsInboundDownlink": true,
                        "statsOutboundUplink": true,
                        "statsOutboundDownlink": true
                    }
                }
            },
            {
                "type": "add_observatory",
                "params": {
                    "subjectSelector": ["proxy"],
                    "probeURL": "https://www.google.com/generate_204",
                    "probeInterval": 5
                }
            },
            {
                "type": "add_balancer",
                "params": {
                    "tag": "balancer-out",
                    "selector": ["proxy"],
                    "strategy": "leastPing",
                    "observation": "observatory"
                }
            },
            {
                "type": "add_outbound",
                "params": {
                    "tag": "proxy",
                    "source": "profile"
                }
            },
            {
                "type": "add_inbound",
                "params": {
                    "tag": "socks",
                    "protocol": "mixed",
                    "port": 10809
                }
            },
            {
                "type": "merge_routing",
                "params": {
                    "inboundTag": "socks",
                    "outboundTag": "balancer-out"
                }
            }
        ]
    }
}
```

## 完整配置片段

将以下 `config_pipeline` 段添加到 `config.json` 根级别：

```json
{
    "config_pipeline": {
        "test": { ... },
        "test_singbox": { ... },
        "standalone": { ... },
        "standalone_singbox": { ... },
        "pool": { ... },
        "pool_singbox": { ... }
    }
}
```

---

**最后更新**: 2026-09-22

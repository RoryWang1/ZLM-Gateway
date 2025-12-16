# 前端环境变量配置

## 快速配置

1. 复制环境变量示例文件：
```bash
cp .env.example .env.development
```

2. 根据实际情况修改配置：
```env
# Gateway API 基础URL
VITE_API_BASE_URL=http://localhost:8080/api/v1

# ZLMediaKit 基础URL（用于播放和快照）
VITE_ZLM_BASE_URL=http://localhost:8081

# ZLMediaKit API Secret（用于快照功能）
# 必须与 configs/zlm_config.ini 中的 secret 保持一致
VITE_ZLM_SECRET=UQyXemwV81qnNkuXQSp2eo5txJM35PZr

# WebSocket URL（用于实时更新）
VITE_WS_URL=ws://localhost:8080/ws
```

## 配置说明

- `VITE_ZLM_BASE_URL`: ZLMediaKit 的 HTTP 服务地址，用于视频播放和快照功能
- `VITE_ZLM_SECRET`: ZLMediaKit API Secret，必须与后端配置一致
- `VITE_API_BASE_URL`: Gateway API 服务地址
- `VITE_WS_URL`: WebSocket 服务地址，用于实时数据更新

## 验证配置

运行配置验证脚本：
```bash
../scripts/verify_zlm_config.sh
```

确保所有配置项都显示 ✅ 绿色标记。

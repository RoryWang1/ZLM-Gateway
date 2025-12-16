# ZLM-Gateway Frontend

ZLM-Gateway 前端展示系统

## 快速开始

### 1. 安装依赖

```bash
# 进入前端目录
cd frontend

# 使用 pnpm（推荐）
pnpm install

# 或使用 npm
npm install

# 或使用 yarn
yarn install
```

### 2. 配置环境变量

环境变量文件已创建，默认配置如下：

- `VITE_API_BASE_URL`: http://localhost:8080/api/v1
- `VITE_ZLM_BASE_URL`: http://localhost:80
- `VITE_WS_URL`: ws://localhost:8080/ws

如需修改，请编辑 `.env.development` 文件。

### 3. 启动开发服务器

```bash
pnpm dev
```

访问 http://localhost:5173

### 4. 构建生产版本

```bash
pnpm build
```

构建产物在 `dist/` 目录

### 5. 预览生产构建

```bash
pnpm preview
```

## 项目结构

```
frontend/
├── src/
│   ├── components/     # 组件
│   │   ├── common/     # 通用组件（Layout、Loading等）
│   │   ├── dashboard/  # 仪表板组件
│   │   ├── streams/    # 流管理组件
│   │   ├── video/      # 视频播放组件
│   │   ├── devices/    # 设备管理组件
│   │   └── monitoring/ # 监控组件
│   ├── pages/          # 页面
│   │   ├── Dashboard.tsx
│   │   ├── Streams.tsx
│   │   ├── VideoView.tsx
│   │   ├── Devices.tsx
│   │   └── Monitoring.tsx
│   ├── api/            # API 客户端
│   │   └── client.ts   # API 基础客户端
│   ├── hooks/          # 自定义 Hooks
│   ├── store/          # 状态管理
│   ├── types/          # TypeScript 类型定义
│   ├── utils/          # 工具函数
│   └── lib/            # 第三方库封装
├── public/             # 静态资源
├── index.html          # HTML 模板
├── vite.config.ts      # Vite 配置
├── tsconfig.json       # TypeScript 配置
└── package.json        # 项目配置
```

## 技术栈

- **框架**: React 18+ + TypeScript 5+
- **构建工具**: Vite 5+
- **路由**: React Router 6+
- **UI组件库**: Ant Design 5+
- **状态管理**: Zustand + TanStack Query
- **HTTP客户端**: Axios
- **视频播放**: flv.js / hls.js / WebRTC
- **图表**: ECharts

## 开发命令

```bash
# 开发服务器
pnpm dev

# 构建生产版本
pnpm build

# 预览生产构建
pnpm preview

# 代码检查
pnpm lint

# 代码格式化
pnpm format

# 类型检查
pnpm type-check
```

## 当前进度

### ✅ Phase 1: 项目初始化（已完成）
- [x] 项目配置（Vite、TypeScript、ESLint、Prettier）
- [x] 基础目录结构
- [x] 路由配置
- [x] Layout 组件（Header、Sidebar）
- [x] 基础页面框架
- [x] API 客户端基础封装
- [x] 类型定义

### 🚧 Phase 2: 基础功能开发（进行中）
- [ ] 流管理 API 实现
- [ ] 设备发现 API 实现
- [ ] 监控 API 实现
- [ ] 状态管理实现

### 📋 Phase 3-6: 待开发
详见 [开发计划文档](../docs/FRONTEND_DEVELOPMENT_PLAN.md)

## 开发指南

详细开发计划请参考：[前端开发计划文档](../docs/FRONTEND_DEVELOPMENT_PLAN.md)

## 注意事项

1. **后端服务**: 确保后端 Gateway 服务已启动（默认端口 8080）
2. **ZLMediaKit**: 确保 ZLMediaKit 服务已启动（默认端口 80）
3. **CORS**: 开发环境已配置代理，生产环境需要配置 CORS
4. **环境变量**: 根据实际部署环境修改 `.env.development` 和 `.env.production`


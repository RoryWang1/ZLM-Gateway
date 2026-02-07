# 优化后的项目架构图 (中文版)

这些图表经过重新设计，旨在提供更清晰、准确且美观的系统架构和工作流视图。它们将之前分散的视图合并为连贯的叙述。

## 1. 系统架构与流摄取管道 (System Architecture & Stream Ingestion)
**变更说明：**
- 将原本横向的“分层”视图（图1）与“转码决策”逻辑（图3）合并。
- 增加了独立的“设备发现”模块，展示其与网关的交互。
- 明确了从 **源 (Source)** -> **网关 (Gateway)** -> **核心处理 (Core Processing)** -> **分发 (Distribution)** 的流向。
- 突出了关键的“转码 vs 代理”决策路径。

```mermaid
graph TD
    %% 样式定义
    classDef source fill:#e1f5fe,stroke:#01579b,stroke-width:2px,rx:5,ry:5;
    classDef gateway fill:#fff9c4,stroke:#fbc02d,stroke-width:2px,rx:5,ry:5;
    classDef core fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,rx:5,ry:5;
    classDef zlm fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,rx:5,ry:5;
    classDef frontend fill:#ffebee,stroke:#c62828,stroke-width:2px,rx:5,ry:5;
    classDef decision fill:#fff3e0,stroke:#ef6c00,stroke-width:2px,shape:diamond;

    %% 子图：输入源
    subgraph Sources ["输入源 (Input Sources)"]
        direction TB
        S1[RTSP 摄像头/流]:::source
        S2[RTMP 推流/拉流]:::source
        S3[HTTP-FLV / HLS]:::source
        S4[GB28181 设备]:::source
        S5[ONVIF / ISAPI / Dahua]:::source
        S6[本地 USB / 文件]:::source
    end

    %% 子图：网关层
    subgraph Gateway_Layer ["网关层 (Gateway Layer)"]
        direction TB
        G1[RTSP Gateway]:::gateway
        G2[RTMP Gateway]:::gateway
        G3[HTTP-FLV / HLS Gateway]:::gateway
        G4[GB28181 Gateway]:::gateway
        G5[设备发现与控制]:::gateway
        G6[Local Gateway]:::gateway
        
        %% 连接
        S1 --> G1
        S2 --> G2
        S3 --> G3
        S4 --> G4
        S5 --> G5
        S6 --> G6
        
        %% 发现逻辑
        G5 -.->|探测/控制| S1
        G5 -.->|探测/控制| S5
    end

    %% 子图：核心处理
    subgraph Core_Logic ["核心处理与管理 (Core Processing)"]
        direction TB
        Smgr[流管理器 StreamManager]:::core
        Pmgr[进程管理器 ProcessManager]:::core
        
        Dec{处理模式决策}:::decision
        
        FF_Trans["FFmpeg 转码"]:::core
        FF_Copy["FFmpeg 封装 (Copy)"]:::core
        Proxy["直连代理 (Native)"]:::core
        
        %% 逻辑流
        G1 & G2 & G3 & G4 & G6 --> Smgr
        Smgr --> Dec
        
        Dec -->|需转码/滤镜| FF_Trans
        Dec -->|仅封装转换| FF_Copy
        Dec -->|标准协议透传| Proxy
        
        Pmgr -.->|管理进程| FF_Trans
        Pmgr -.->|管理进程| FF_Copy
        Pmgr -.->|管理进程| Proxy
    end

    %% 子图：媒体服务器
    subgraph Media_Server [ZLMediaKit 流媒体服务器]
        ZLM[ZLMediaKit 核心]:::zlm
        
        P_HLS[HLS 输出]:::zlm
        P_HTTP[HTTP-FLV 输出]:::zlm
        P_WebRTC[WebRTC 输出]:::zlm
        
        %% 推流注入
        FF_Trans -->|推流| ZLM
        FF_Copy -->|推流| ZLM
        Proxy -->|推流| ZLM
        
        ZLM --> P_HLS
        ZLM --> P_HTTP
        ZLM --> P_WebRTC
    end
    
    %% 子图：前端应用
    subgraph Client_Layer ["前端应用 (Frontend Application)"]
        FE[VideoGrid 视频墙]:::frontend
        Player[通用播放器组件]:::frontend
        
        P_HLS -.->|拉流| Player
        P_WebRTC -.->|拉流| Player
        P_HTTP -.->|拉流| Player
        
        FE -->|API 控制| Smgr
    end

    %% 全局样式
    linkStyle default stroke:#666,stroke-width:1px;
```

## 2. 流启动与播放全流程 (Stream Activation & Playback Sequence)
**变更说明：**
- 将“后端启动流程”（图5）与“前端播放器选择逻辑”（图2）合并。
- 形成了一个从用户点击“播放”到看到视频画面的完整“用户故事”。
- 简化了内部网关的检查细节，但保留了关键的“逻辑分支”。

```mermaid
sequenceDiagram
    autonumber
    actor User as 用户
    participant FE as "前端 (UI/VideoGrid)"
    participant API as Gateway API
    participant SM as "StreamManager (流管理)"
    participant PM as "ProcessManager (进程管理)"
    participant FF as "FFmpeg/Proxy (工作进程)"
    participant Source as "远端源 (Camera/Stream)"
    participant ZLM as "ZLMediaKit (媒体服务)"

    User->>FE: 点击流的“播放”按钮
    FE->>API: POST /api/v1/streams/start (id, type)
    
    activate API
    API->>SM: 检查流是否已在运行?
    
    alt 流已在运行
        SM-->>API: 返回现有流信息
    else 流未运行
        API->>SM: 初始化流协议
        SM->>SM: 决策: 转码 / Copy / 代理
        
        SM->>PM: 请求启动进程
        activate PM
        
        alt 需要转码 (Codec Incompatible)
            PM->>FF: 启动 FFmpeg (Transcode)
        else 仅封装转换 (Container Incompatible)
            PM->>FF: 启动 FFmpeg (Copy)
        else 直接代理 (Native Proxy)
            PM->>FF: 启动 Proxy 任务
        end
        

        
        FF->>Source: 建立连接 (RTSP/HTTP)
        Source-->>FF: 传输媒体流
        FF->>ZLM: 推流 (RTSP/RTMP)
        
        activate ZLM
        ZLM-->>FF: 确认推流成功
        deactivate ZLM
        
        PM-->>SM: 进程启动成功 (PID)
        deactivate PM
        
        SM->>SM: 注册流元数据
        
        par WebSocket 更新
            SM->>FE: 广播 "StreamStarted" 事件
        and HTTP 响应
            SM-->>API: 返回流信息 (输出 URLs)
        end
    end
    
    API-->>FE: 返回 JSON { ws_url, http_url, protocol }
    deactivate API
    
    %% 前端决策逻辑 (合并自图2)
    Note over FE: 前端播放器选择逻辑
    
    FE->>FE: 解析 `output_protocol`
    
    alt 协议 == HLS
        FE->>FE: 初始化 HLS.js
        FE->>ZLM: 请求 .m3u8
    else 协议 == HTTP-FLV
        FE->>FE: 初始化 mpegts/flv.js
        FE->>ZLM: 请求 .flv
    else 协议 == WebRTC
        FE->>FE: 初始化 ZLMRTCClient
        FE->>ZLM: WebRTC 信令交互
    end
    
    
    ZLM-->>FE: 传输视频流数据
    FE-->>User: 渲染播放画面
```

## 3. 流停止与清理流程 (Stream Termination & Cleanup)
**变更说明：**
- 优化了复杂的“删除流程”（图4）。
- 聚焦于“资源清理”链条：API -> 进程 -> 流注册 -> 通知。
- 将错误处理（404/软失败）隐式处理，不再让主流程显得杂乱。

```mermaid
sequenceDiagram
    autonumber
    actor User as 用户
    participant FE as 前端
    participant API as Gateway API
    participant SM as StreamManager
    participant PM as ProcessManager
    participant ZLM as ZLMediaKit

    User->>FE: 点击 “删除” 或 “停止”
    FE->>User: 显示确认对话框
    User->>FE: 确认操作
    
    FE->>API: DELETE /api/v1/streams/:id
    activate API
    
    API->>SM: 获取流元数据
    
    alt 流不存在
        API-->>FE: 返回 404 错误
    else 流存在
        API->>SM: 标记状态 = STOPPING
        
        %% 停止后端进程
        API->>PM: 停止对应进程 (PID)
        activate PM
        PM->>PM: 发送 Kill 信号给 FFmpeg/Proxy
        PM-->>API: 进程已停止
        deactivate PM
        
        %% 清理 ZLM
        API->>ZLM: 关闭流 (API/Hook)
        
        %% 清理管理器
        API->>SM: 注销流 (Unregister)
        
        par 通知客户端
            SM->>FE: WebSocket 广播 "StreamStopped"
        and 由于请求完成
            API-->>FE: 返回 200 OK
        end
    end
    
    deactivate API
    
    FE->>FE: 从列表中移除流
    FE->>User: 提示 "流已删除"
```

## 4. 设备自动发现流程 (Device Discovery Workflow)
**新增说明：**
- 补充了项目中关于 ONVIF/ISAPI 等协议的自动发现机制。
- 展示了从前端发起扫描到后端探测网段的异步交互过程。

```mermaid
sequenceDiagram
    autonumber
    actor User as 用户
    participant FE as "前端 (设备页)"
    participant API as Gateway API
    participant DG as "DiscoveryGateway (发现服务)"
    participant Worker as "扫描工作线程"
    participant Device as "目标设备 (ONVIF/ISAPI)"

    User->>FE: 点击 "扫描设备"
    FE->>API: POST /api/v1/discovery/scan (协议, 网段)
    
    activate API
    API->>DG: 启动扫描任务
    DG->>Worker: 异步派发扫描指令
    API-->>FE: 返回任务ID (TaskID)
    deactivate API
    
    Processing->>User: 显示 "扫描中..."
    
    activate Worker
    loop 每个IP地址 (扫描网段)
        Worker->>Device: 发送探测包 (Probe/Broadcast)
        
        alt 设备响应
            Device-->>Worker: 响应 (设备信息/地址)
            Worker->>Worker: 解析 XML/JSON
            Worker->>DG: 缓存临时设备列表
        end
    end
    deactivate Worker
    
    loop 轮询结果 (或 WebSocket 推送)
        FE->>API: GET /api/v1/discovery/result?task_id=xxx
        API->>DG: 获取当前缓存列表
        DG-->>API: 返回设备列表
        API-->>FE: JSON [Device List]
        FE->>User: 动态更新设备列表
    end
    
    User->>FE: 点击 "添加到系统"
    FE->>API: POST /api/v1/streams/add
```

## 5. 进程保活与监控状态机 (Process Monitoring State Machine)
**新增说明：**
- 描述了 `ProcessManager` 复杂的内部状态流转。
- 展示了系统如何处理异常退出、自动重启以及分层监控策略。

```mermaid
stateDiagram-v2
    direction LR
    
    [*] --> Stopped: 初始状态 (Initial)
    
    Stopped --> Starting: 接收启动指令
    Starting --> Running: 进程 PID 产生 & 存活
    
    state Running {
        direction TB
        [*] --> NormalMonitor: 标准监控 (5s)
        NormalMonitor --> CriticalMonitor: 发现异常/高负载
        CriticalMonitor --> NormalMonitor: 恢复正常
        NormalMonitor --> MinimalMonitor: 长期稳定 (60s)
    }
    
    Running --> Stopping: 接收停止指令
    Stopping --> Stopped: 进程退出
    
    Running --> Error: 异常退出 (Crash/EOF)
    
    state Error {
        direction TB
        CheckPolicy: 检查重启策略
        WaitOff: 指数退避等待
        
        [*] --> CheckPolicy
        CheckPolicy --> WaitOff: 剩余重启次数 > 0
        WaitOff --> Restarting: 等待结束
    }
    
    Restarting --> Starting: 尝试重启
    Error --> Stopped: 重启耗尽 / 致命错误 (配置错)
```

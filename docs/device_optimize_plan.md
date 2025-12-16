设备发现重构 - 实施计划
🎯 目标
统一架构 - 合并 LocalCameraHandler 到 DeviceHandler
移除冗余 - 删除不必要的 Adapter 层
保留特性 - 确保 LocalCamera 的流转化逻辑完整保留
🔍 LocalCamera 特殊性分析
LocalCamera 的独特流程
LocalCamera → FFmpeg → ZLMediaKit
    ↓
1. 设备发现（系统摄像头列表）
2. FFmpeg命令构建（特殊！）
   - BuildInput: avfoundation (macOS) / v4l2 (Linux)
   - BuildEncodingParams: 音视频编解码
   - BuildOutput: -f flv rtmp://zlm/app/stream
3. 进程管理 (ProcessManager)
4. 流验证 (StreamValidator)
5. 健康监控 (HealthMonitor)
6. 智能码率分配 (BitrateAllocator)
关键差异:

❗FFmpeg 转化: 本地设备 → RTMP推流到ZLM（独有）
❗复杂流程: BuildCommand + 进程管理 + 健康监控
❗专用组件: FFmpegCommandBuilder, HealthMonitor, StreamValidator
其他设备的流程
ONVIF/ISAPI/Dahua/PSIA/GB28181
    ↓
1. 网络发现
2. 设备管理（CRUD）
3. RTSP URL获取
4. 直接推流（无转化）
核心差异: 不需要 FFmpeg 转化，直接使用设备RTSP流

✅ 保留策略
必须保留的 LocalCamera 组件
✅ FFmpegCommandBuilder - 命令构建
✅ DeviceManager - 设备管理
✅ DeviceResolver - 设备解析
✅ StreamValidator - 流验证
✅ HealthMonitor - 健康监控
✅ BitrateAllocator - 码率分配
可以统一的部分
✅ 设备发现API - 都是 Discover/List/Get
✅ 设备CRUD操作 - 统一接口
✅ API Handler - 合并到 DeviceHandler
📋 实施方案
Phase 1: 架构准备（准备阶段）
1.1 创建统一的设备处理基类
文件: src/api/handlers/device_handler_base.hpp

namespace api {
namespace handlers {
// 设备操作助手 - 模板函数消除重复
class DeviceHandlerHelper {
public:
    // 通用设备发现处理
    template<typename TGateway, typename TDevice>
    static void HandleDiscover(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const httplib::Request& req,
        httplib::Response& res,
        std::function<std::vector<TDevice>(TGateway*, const httplib::Request&)> discover_func,
        std::function<json(const TDevice&)> to_json_func
    ) {
        try {
            if (!gateway) {
                ResponseHelper::Error(res, -1, protocol_name + " Gateway not enabled", 500);
                return;
            }
            
            auto devices = discover_func(gateway.get(), req);
            
            json response_data = {
                {"devices", json::array()},
                {"count", devices.size()}
            };
            
            for (const auto& device : devices) {
                response_data["devices"].push_back(to_json_func(device));
            }
            
            ResponseHelper::Success(res, response_data, "Device discovery completed");
        } catch (const std::exception& e) {
            ResponseHelper::Error(res, e);
        }
    }
    
    // 通用设备列表处理
    template<typename TGateway, typename TDevice>
    static void HandleList(
        std::shared_ptr<TGateway> gateway,
        const std::string& protocol_name,
        const httplib::Request& req,
        httplib::Response& res,
        std::function<std::vector<TDevice>(TGateway*)> list_func,
        std::function<json(const TDevice&)> to_json_func
    ) {
        // 类似实现...
    }
    
    // 其他通用方法...
};
} // namespace handlers
} // namespace api
1.2 扩展 DeviceHandler 接口
修改: 
src/api/handlers/device_handler.hpp

class DeviceHandler {
public:
    DeviceHandler(
        // 现有参数...
        std::shared_ptr<gateway::LocalCameraGateway> local_camera_gateway,  // 新增
        std::shared_ptr<streaming::StreamManager> stream_manager  // 新增，用于LocalCamera
    );
    
    // ===== ONVIF =====
    void HandleDiscoverDevices(const httplib::Request& req, httplib::Response& res);
    void HandleGetDevices(const httplib::Request& req, httplib::Response& res);
    // ... 其他ONVIF方法
    
    // ===== LocalCamera（新增）=====
    void HandleDiscoverLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleRefreshLocalCameras(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCamera(const httplib::Request& req, httplib::Response& res);
    void HandleGetLocalCameraCapabilities(const httplib::Request& req, httplib::Response& res);
    void HandleStartLocalCameraStream(const httplib::Request& req, httplib::Response& res);
    void HandleStopLocalCameraStream(const httplib::Request& req, httplib::Response& res);
    
private:
    // 现有成员...
    std::shared_ptr<gateway::LocalCameraGateway> local_camera_gateway_;  // 新增
    std::shared_ptr<streaming::StreamManager> stream_manager_;  // 新增
};
Phase 2: Handler 整合（实施阶段）
2.1 移植 LocalCameraHandler 方法到 DeviceHandler
操作: 复制 
local_camera_handler.cpp
 的所有方法到 
device_handler.cpp

要点:

✅ 保留所有 LocalCamera 特殊逻辑
✅ 保留 FFmpeg 流转化逻辑
✅ 保留 StreamManager 集成
✅ 保留错误处理细节
示例:

// device_handler.cpp
// LocalCamera 设备发现
void DeviceHandler::HandleDiscoverLocalCameras(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::LocalCameraGateway, gateway::LocalCameraDevice>(
        local_camera_gateway_,
        "LocalCamera",
        req, res,
        [](auto* gw, const auto& /*req*/) {
            return gw->DiscoverCameras();  // 调用 LocalCamera 特殊方法
        },
        [](const auto& device) {
            return json{
                {"device_id", device.device_id},
                {"camera_name", device.camera_name},
                {"index", device.index},
                // ... LocalCamera 特定字段
            };
        }
    );
}
// LocalCamera 流启动 **保留完整逻辑**
void DeviceHandler::HandleStartLocalCameraStream(const httplib::Request& req, httplib::Response& res) {
    // 完整复制 LocalCameraHandler::HandleStartLocalCameraStream 的实现
    try {
        if (!local_camera_gateway_) {
            api::utils::ResponseHelper::Error(res, -1, "Local Camera Gateway not enabled", 500);
            return;
        }
        json body = json::parse(req.body);
        // ... 完全相同的逻辑，不做任何改动
        
        // ⚠️ 关键：保留与 StreamManager 的交互
        bool success = local_camera_gateway_->StartCameraStream(device_id, app, stream, output_protocol);
        // ... 完全相同的后续处理
    } catch (...) {
        // ... 完全相同的错误处理
    }
}
2.2 更新路由注册
修改: 
src/main.cpp
 (或路由注册位置)

// 注册 LocalCamera 路由到 DeviceHandler
server.Post("/api/v1/cameras/discover", [&](<auto& req, auto& res>) {
    device_handler->HandleDiscoverLocalCameras(req, res);
});
server.Get("/api/v1/cameras", [&](<auto& req, auto& res>) {
    device_handler->HandleGetLocalCameras(req, res);
});
server.Post("/api/v1/cameras/refresh", [&](<auto& req, auto& res>) {
    device_handler->HandleRefreshLocalCameras(req, res);
});
server.Get("/api/v1/cameras/:device_id", [&](<auto& req, auto& res>) {
    device_handler->HandleGetLocalCamera(req, res);
});
server.Get("/api/v1/cameras/:device_id/capabilities", [&](<auto& req, auto& res>) {
    device_handler->HandleGetLocalCameraCapabilities(req, res);
});
server.Post("/api/v1/cameras/start", [&](<auto& req, auto& res>) {
    device_handler->HandleStartLocalCameraStream(req, res);
});
server.Post("/api/v1/cameras/stop", [&](<auto& req, auto& res>) {
    device_handler->HandleStopLocalCameraStream(req, res);
});
2.3 删除 LocalCameraHandler
操作:

删除 
src/api/handlers/local_camera_handler.hpp
删除 
src/api/handlers/local_camera_handler.cpp
更新 
main.cpp
 中的依赖
Phase 3: Adapter 移除（优化阶段）
3.1 使用模板函数直接调用 Gateway
示例: ONVIF设备发现

// device_handler.cpp
void DeviceHandler::HandleDiscoverDevices(const httplib::Request& req, httplib::Response& res) {
    DeviceHandlerHelper::HandleDiscover<gateway::ONVIFGateway, gateway::ONVIFDevice>(
        onvif_gateway_,
        "ONVIF",
        req, res,
        [](auto* gw, const auto& req) {
            // 解析参数
            int timeout = 5;
            if (!req.body.empty()) {
                try {
                    auto body = json::parse(req.body);
                    timeout = body.value("timeout_seconds", 5);
                } catch (...) {}
            }
            return gw->DiscoverDevices(timeout);
        },
        [](const auto& d) {
            return json{
                {"device_id", d.device_id},
                {"name", d.name},
                {"manufacturer", d.manufacturer},
                // ... ONVIF 特定字段
            };
        }
    );
}
3.2 逐个协议迁移
顺序:

✅ ONVIF (简单，只需要timeout参数)
✅ GB28181 (中等，类似ONVIF)
✅ ISAPI (复杂，ip_range + port)
✅ Dahua (同ISAPI)
✅ PSIA (同ISAPI)
3.3 删除 Adapter 文件
操作:

删除 src/api/handlers/adapters/*.cpp
删除 src/api/handlers/adapters/*.hpp
删除 
src/api/handlers/device_adapter.hpp
删除 
src/api/handlers/device_adapter_registry.cpp
删除 
src/api/handlers/device_adapter_registry.hpp
🔄 代码变更对比
Before (当前架构)
DeviceHandler (300+ lines)
├── HandleDiscoverDevices → ONVIFDeviceAdapter::HandleDiscover
├── HandleDiscoverISAPIDevices → ISAPIDeviceAdapter::HandleDiscover
├── ... (30+ methods)
LocalCameraHandler (300+ lines)
├── HandleDiscoverLocalCameras
├── HandleStartLocalCameraStream
├── ...
ONVIFDeviceAdapter (250 lines)
ISAPIDeviceAdapter (250 lines)
DahuaDeviceAdapter (250 lines)
PSIADeviceAdapter (250 lines)
GB28181DeviceAdapter (250 lines)
After (重构后)
DeviceHandler (600 lines)
├── HandleDiscoverDevices (直接调用 ONVIFGateway)
├── HandleDiscoverISAPIDevices (直接调用 ISAPIGateway)
├── HandleDiscoverLocalCameras (直接调用 LocalCameraGateway)
├── HandleStartLocalCameraStream (保留完整逻辑)
├── ... (所有设备操作统一)
DeviceHandlerHelper (200 lines, 模板函数)
├── HandleDiscover<T>
├── HandleList<T>
├── ...
节省: ~1300 lines (-65%)

⚠️ 关键注意事项
1. LocalCamera 流转化逻辑 - 完全保留
必须保留:

// LocalCamera 的特殊流程
StartCameraStream() {
    1. 设备验证
    2. 构建 FFmpeg 命令 ← 关键！
    3. 启动 FFmpeg 进程 ← 关键！
    4. 流验证
    5. 健康监控启动 ← 关键！
    6. 码率分配 ← 关键！
}
不要改动:

FFmpegCommandBuilder
HealthMonitor
StreamValidator
BitrateAllocator
ProcessManager集成
2. StreamManager 集成 - 保留
// LocalCamera 需要 StreamManager
HandleStartLocalCameraStream() {
    // ... 启动流
    
    // StreamManager 不会自动注册，需要在 API Handler 层处理
    // （通过 LocalCameraGateway 的回调）
}
HandleStopLocalCameraStream() {
    // ... 停止流
    stream_manager_->UnregisterStream(app, stream);  ← 保留
}
3. API 兼容性 - 保持
不改变现有API端点:

/api/v1/cameras/* - LocalCamera API
/api/v1/devices/onvif - ONVIF API
... (其他协议)
📝 测试计划
Phase 1 测试
 DeviceHandlerHelper 模板函数编译
 DeviceHandler 构造函数更新编译
Phase 2 测试
 LocalCamera 设备发现
 LocalCamera 流启动/停止 ← 关键测试
 LocalCamera FFmpeg 进程启动
 LocalCamera 健康监控
 LocalCamera 码率分配
 StreamManager 集成
 所有 LocalCamera API 端点
Phase 3 测试
 ONVIF 设备发现（无 Adapter）
 ISAPI 设备发现（无 Adapter）
 所有协议的设备 CRUD
 端到端测试
🎯 成功标准
✅ LocalCamera 流启动成功
✅ FFmpeg 进程正常运行
✅ ZLM 中流可用
✅ 健康监控正常工作
✅ 所有设备API正常
✅ 代码量减少 60%+
✅ 架构统一
📅 时间估算
阶段	任务	时间
Phase 1	架构准备	2小时
Phase 2	Handler整合	3小时
Phase 3	Adapter移除	3小时
测试验证	全面测试	2小时
总计		10小时


⏳ Phase 1 完成进度报告
✅ 已完成
✅ 创建 device_handler_helper.hpp 模板助手类
HandleDiscover模板
HandleList模板
HandleGet模板
✅ 扩展 device_handler.hpp
添加 LocalCameraGateway 前向声明
添加 StreamManager 前向声明
添加 7 个 LocalCamera 方法声明
添加私有成员变量
✅ 编译验证通过


✅ Phase 2 完成进度报告
✅ 已完成
✅ 更新 DeviceHandler 构造函数，添加 LocalCameraGateway 和 StreamManager 参数
✅ 迁移所有 LocalCamera 方法到 device_handler.cpp（7个方法）
  - HandleDiscoverLocalCameras
  - HandleGetLocalCameras
  - HandleRefreshLocalCameras
  - HandleGetLocalCamera
  - HandleGetLocalCameraCapabilities
  - HandleStartLocalCameraStream（保留完整 FFmpeg 流转化逻辑）
  - HandleStopLocalCameraStream
✅ 更新 http_server.cpp 中的路由注册，将所有 local_camera_handler_ 改为 device_handler_
✅ 从 http_server.hpp 移除 LocalCameraHandler 的 include 和成员变量
✅ 从 http_server.cpp 的 InitializeHandlers 中移除 LocalCameraHandler 初始化
✅ 更新 DeviceHandler 初始化，添加 local_camera_gateway_ 和 stream_manager_ 参数
✅ 删除 LocalCameraHandler 文件（.hpp 和 .cpp）
✅ 编译验证通过（无 linter 错误）

🔄 下一步：Phase 3 - Adapter 移除
即将开始移除 Adapter 层，直接调用 Gateway。
# 测试脚本说明

## 脚本列表

### 1. `quick_test.sh` - 快速测试（推荐日常使用）
**特点**:
- 快速执行（3-5秒超时）
- 非阻塞
- 准确识别超时和错误
- 显示流统计信息

**使用**:
```bash
bash frontend/scripts/quick_test.sh
```

**输出**:
- Gateway API 状态
- 流数量和协议统计
- 前端服务器状态

### 2. `accurate_test.sh` - 准确测试（推荐完整测试）
**特点**:
- 完整的服务进程检查
- 详细的 API 测试
- 正确处理超时（macOS 兼容）
- 彩色输出和统计

**使用**:
```bash
bash frontend/scripts/accurate_test.sh
```

**输出**:
- 服务进程状态（Gateway、ZLMediaKit、前端）
- Gateway API 测试结果
- 设备发现 API 测试结果
- 前端服务器测试结果
- ZLMediaKit API 测试结果
- 测试统计（通过/失败/警告）

### 3. `comprehensive_test.sh` - 完整测试（详细版本）
**特点**:
- 最详细的测试
- 包含所有 API 端点
- 完整的错误处理

**使用**:
```bash
bash frontend/scripts/comprehensive_test.sh
```

## 脚本改进

### 问题修复

1. **超时处理**: 
   - 使用 `curl -m` 而不是 `timeout` 命令（macOS 兼容）
   - 正确识别超时错误（exit code 28）

2. **错误识别**:
   - 区分超时、连接失败、HTTP 错误
   - 显示准确的错误信息

3. **数据解析**:
   - 正确处理 JSON 响应
   - 支持不同的响应格式（数组或对象）
   - 统计流数量和协议类型

4. **非阻塞**:
   - 所有命令都有超时设置
   - 不会因为单个 API 失败而阻塞

## 测试结果说明

### 状态标识
- ✅ 通过: 测试成功
- ❌ 失败: 测试失败（需要修复）
- ⚠️ 警告: 测试超时或部分失败（可能需要检查）

### 常见问题

1. **Gateway API 超时**
   - 可能原因: Gateway 进程运行但 API 服务器未正常启动
   - 检查: `tail -50 logs/gateway.log`
   - 检查: `lsof -i :8080`

2. **流数量为 0**
   - 可能原因: 没有运行的流
   - 正常情况: 如果确实没有流，这是正常的

3. **前端服务器未运行**
   - 解决方法: `cd frontend && npm run dev`

## 使用建议

- **日常快速检查**: 使用 `quick_test.sh`
- **完整测试**: 使用 `accurate_test.sh`
- **详细诊断**: 使用 `comprehensive_test.sh`

---

**最后更新**: 2025-11-22


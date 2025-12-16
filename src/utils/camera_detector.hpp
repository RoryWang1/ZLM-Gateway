#ifndef UTILS_CAMERA_DETECTOR_HPP
#define UTILS_CAMERA_DETECTOR_HPP

#include <memory>
#include <string>
#include <vector>

namespace utils {

/**
 * @brief 音频设备信息
 */
struct AudioDevice {
  int index;        // 音频设备索引
  std::string name; // 音频设备名称
};

/**
 * @brief 摄像头设备信息
 */
struct CameraDevice {
  std::string device_id; // 设备ID（如 "local-camera-1"）
  std::string name;      // 设备名称（如 "HD webcam"）
  int index; // 设备索引（macOS: avfoundation索引, Linux: /dev/video索引）
  std::string platform;    // 平台（"macos" 或 "linux"）
  std::string device_path; // 设备路径（Linux: "/dev/video0", macOS: 空）
  int audio_device_index = -1; // 匹配的音频设备索引（-1 表示未找到或不需要）

  // 设备能力（可选）
  std::vector<std::string> supported_resolutions; // 支持的分辨率列表
  std::vector<int> supported_fps;                 // 支持的帧率列表
};

/**
 * @brief 摄像头设备检测器
 *
 * 跨平台摄像头设备检测工具类
 */
class CameraDetector {
public:
  /**
   * @brief 构造函数
   * @param ffmpeg_path FFmpeg二进制路径
   */
  CameraDetector(const std::string &ffmpeg_path);

  /**
   * @brief 析构函数
   */
  ~CameraDetector() = default;

  /**
   * @brief 发现所有可用的摄像头设备
   * @return 摄像头设备列表
   */
  std::vector<CameraDevice> DiscoverCameras();

  /**
   * @brief 获取指定索引的摄像头设备信息
   * @param index 设备索引
   * @return 摄像头设备信息，不存在返回空对象（device_id为空）
   */
  CameraDevice GetCamera(int index);

  /**
   * @brief 发现所有音频设备（macOS only）
   * @return 音频设备列表
   */
  std::vector<AudioDevice> DiscoverAudioDevices();

  /**
   * @brief 为摄像头设备匹配音频设备
   * @param camera_name 摄像头名称
   * @param audio_devices 音频设备列表
   * @return 匹配的音频设备索引，-1 表示未找到
   */
  int MatchAudioDevice(const std::string &camera_name,
                       const std::vector<AudioDevice> &audio_devices) const;

  /**
   * @brief 查询摄像头设备能力（支持的分辨率和帧率）
   * @param camera_index 摄像头索引
   * @return 设备能力信息（分辨率列表和帧率列表）
   */
  std::pair<std::vector<std::string>, std::vector<int>>
  QueryCameraCapabilities(int camera_index) const;

private:
  /**
   * @brief 在macOS上发现摄像头（使用avfoundation）
   * @return 摄像头设备列表
   */
  std::vector<CameraDevice> DiscoverCamerasMacOS();

  /**
   * @brief 在Linux上发现摄像头（使用v4l2）
   * @return 摄像头设备列表
   */
  std::vector<CameraDevice> DiscoverCamerasLinux();

  /**
   * @brief 执行FFmpeg命令并获取输出
   * @param command FFmpeg命令
   * @return 命令输出
   */
  std::string ExecuteFFmpegCommand(const std::string &command) const;

  /**
   * @brief 生成设备ID（基于设备名称，确保稳定性）
   * @param name 设备名称
   * @param platform 平台名称
   * @param device_path 设备路径（可选，用于Linux）
   * @return 设备ID
   */
  std::string GenerateDeviceID(const std::string &name,
                               const std::string &platform,
                               const std::string &device_path = "") const;

  std::string ffmpeg_path_;
};

} // namespace utils

#endif // UTILS_CAMERA_DETECTOR_HPP

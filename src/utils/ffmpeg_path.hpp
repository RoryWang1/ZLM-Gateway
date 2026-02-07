#ifndef UTILS_FFMPEG_PATH_HPP
#define UTILS_FFMPEG_PATH_HPP

#include <cstdlib>
#include <filesystem>
#include <string>

namespace utils {

/**
 * @brief 获取项目内 FFmpeg 二进制路径
 *
 * 自动检测操作系统架构，返回项目内 FFmpeg 二进制文件的路径。
 * 如果项目内不存在，则返回系统默认路径。
 *
 * @return FFmpeg 二进制文件的绝对路径
 */
inline std::string GetFFmpegPath() {
  namespace fs = std::filesystem;

  // 获取项目根目录（假设可执行文件在 bin/ 目录下）
  // 或者通过环境变量 PROJECT_ROOT 获取
  std::string project_root;
  const char *env_root = std::getenv("PROJECT_ROOT");
  if (env_root) {
    project_root = env_root;
  } else {
    // 尝试从当前工作目录推断
    fs::path current_path = fs::current_path();
    // 如果当前在项目根目录
    if (fs::exists(current_path / "third_party" / "ffmpeg")) {
      project_root = current_path.string();
    } else if (fs::exists(current_path.parent_path() / "third_party" /
                          "ffmpeg")) {
      // 如果在 bin/ 目录下
      project_root = current_path.parent_path().string();
    } else {
      // 回退到系统路径
      return "/usr/bin/ffmpeg";
    }
  }

  // 检测平台
  std::string platform;
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
  platform = "macos-arm64";
#else
  platform = "macos-x86_64";
#endif
#elif defined(__linux__)
  platform = "linux";
#else
  // 未知平台，使用系统路径
  return "/usr/bin/ffmpeg";
#endif

  // 构建路径
  fs::path ffmpeg_path =
      fs::path(project_root) / "third_party" / "ffmpeg" / platform / "ffmpeg";

  // 检查文件是否存在
  if (fs::exists(ffmpeg_path) && fs::is_regular_file(ffmpeg_path)) {
    return fs::absolute(ffmpeg_path).string();
  }

  // 回退到系统路径
  return "/usr/bin/ffmpeg";
}

/**
 * @brief 获取项目内 FFprobe 二进制路径
 *
 * @return FFprobe 二进制文件的绝对路径
 */
inline std::string GetFFprobePath() {
  namespace fs = std::filesystem;

  std::string project_root;
  const char *env_root = std::getenv("PROJECT_ROOT");
  if (env_root) {
    project_root = env_root;
  } else {
    fs::path current_path = fs::current_path();
    if (fs::exists(current_path / "third_party" / "ffmpeg")) {
      project_root = current_path.string();
    } else if (fs::exists(current_path.parent_path() / "third_party" /
                          "ffmpeg")) {
      project_root = current_path.parent_path().string();
    } else {
      return "/usr/bin/ffprobe";
    }
  }

  std::string platform;
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
  platform = "macos-arm64";
#else
  platform = "macos-x86_64";
#endif
#elif defined(__linux__)
  platform = "linux";
#else
  return "/usr/bin/ffprobe";
#endif

  fs::path ffprobe_path =
      fs::path(project_root) / "third_party" / "ffmpeg" / platform / "ffprobe";

  if (fs::exists(ffprobe_path) && fs::is_regular_file(ffprobe_path)) {
    return fs::absolute(ffprobe_path).string();
  }

  return "/usr/bin/ffprobe";
}

} // namespace utils

#endif // UTILS_FFMPEG_PATH_HPP

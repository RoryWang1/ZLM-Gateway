#include "utils/camera_detector.hpp"
#include "utils/logger.hpp"
#include <sstream>
#include <functional>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef __APPLE__
    #define PLATFORM_MACOS
#elif __linux__
    #define PLATFORM_LINUX
#endif

namespace utils {

CameraDetector::CameraDetector(const std::string& ffmpeg_path)
    : ffmpeg_path_(ffmpeg_path.empty() ? "ffmpeg" : ffmpeg_path) {
}

std::vector<CameraDevice> CameraDetector::DiscoverCameras() {
#ifdef PLATFORM_MACOS
    return DiscoverCamerasMacOS();
#elif PLATFORM_LINUX
    return DiscoverCamerasLinux();
#else
    LOG_WARN("[CameraDetector] 不支持的操作系统平台");
    return std::vector<CameraDevice>();
#endif
}

std::vector<CameraDevice> CameraDetector::DiscoverCamerasMacOS() {
    std::vector<CameraDevice> devices;
    
    // 使用FFmpeg的avfoundation列出设备
    std::string command = ffmpeg_path_ + " -f avfoundation -list_devices true -i \"\" 2>&1";
    LOG_DEBUG("[CameraDetector] macOS: 执行命令: {}", command);
    std::string output = ExecuteFFmpegCommand(command);
    
    if (output.empty()) {
        LOG_WARN("[CameraDetector] macOS: FFmpeg命令执行失败或输出为空");
        return devices;
    }
    
    LOG_DEBUG("[CameraDetector] macOS: FFmpeg输出长度: {} 字节", output.length());
    
    // 解析输出，查找视频设备
    std::istringstream iss(output);
    std::string line;
    bool in_video_section = false;
    int device_index = 0;
    
    while (std::getline(iss, line)) {
        // 查找视频设备部分
        if (line.find("AVFoundation video devices") != std::string::npos) {
            in_video_section = true;
            LOG_DEBUG("[CameraDetector] macOS: 进入视频设备部分");
            continue;
        }
        
        // 如果遇到音频设备部分，停止解析
        if (line.find("AVFoundation audio devices") != std::string::npos) {
            LOG_DEBUG("[CameraDetector] macOS: 遇到音频设备部分，停止解析");
            break;
        }
        
        if (in_video_section) {
            // 查找设备行，格式: [AVFoundation indev @ ...] [0] Device Name
            // 需要找到最后一个 [数字] 模式
            size_t last_bracket_start = line.rfind('[');
            size_t last_bracket_end = line.rfind(']');
            
            if (last_bracket_start != std::string::npos && last_bracket_end != std::string::npos && 
                last_bracket_end > last_bracket_start) {
                try {
                    std::string index_str = line.substr(last_bracket_start + 1, last_bracket_end - last_bracket_start - 1);
                    // 检查是否是纯数字
                    bool is_numeric = true;
                    for (char c : index_str) {
                        if (!std::isdigit(c)) {
                            is_numeric = false;
                            break;
                        }
                    }
                    
                    if (is_numeric && !index_str.empty()) {
                        device_index = std::stoi(index_str);
                        std::string device_name = line.substr(last_bracket_end + 1);
                        
                        // 去除首尾空格
                        size_t first = device_name.find_first_not_of(" \t");
                        size_t last = device_name.find_last_not_of(" \t");
                        if (first != std::string::npos && last != std::string::npos) {
                            device_name = device_name.substr(first, last - first + 1);
                        }
                        
                        if (!device_name.empty()) {
                            CameraDevice device;
                            device.name = device_name;
                            device.index = device_index;
                            device.platform = "macos";
                            device.device_path = "";  // macOS不使用设备路径
                            device.device_id = GenerateDeviceID(device_name, "macos", "");
                            
                            devices.push_back(device);
                            LOG_INFO("[CameraDetector] macOS: 发现摄像头 [{}] {} (device_id: {})", device_index, device_name, device.device_id);
                        } else {
                            LOG_DEBUG("[CameraDetector] macOS: 设备名称为空，跳过: {}", line);
                        }
                    } else {
                        LOG_DEBUG("[CameraDetector] macOS: 索引不是纯数字，跳过: {}", line);
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("[CameraDetector] macOS: 解析设备索引失败: {}, 行: {}", e.what(), line);
                }
            }
        }
    }
    
    LOG_INFO("[CameraDetector] macOS: 发现 {} 个摄像头设备", devices.size());
    return devices;
}

std::vector<CameraDevice> CameraDetector::DiscoverCamerasLinux() {
    std::vector<CameraDevice> devices;
    
    // 扫描 /dev/video* 设备
    namespace fs = std::filesystem;
    std::string dev_video_dir = "/dev";
    
    if (!fs::exists(dev_video_dir)) {
        LOG_WARN("[CameraDetector] Linux: /dev 目录不存在");
        return devices;
    }
    
    // 遍历 /dev/video* 设备
    for (int i = 0; i < 32; ++i) {  // 通常最多32个视频设备
        std::string device_path = "/dev/video" + std::to_string(i);
        
        if (fs::exists(device_path) && fs::is_character_file(device_path)) {
            // 尝试使用FFmpeg检测设备信息
            std::string command = ffmpeg_path_ + " -f v4l2 -list_formats all -i " + device_path + " 2>&1";
            std::string output = ExecuteFFmpegCommand(command);
            
            // 如果FFmpeg能够识别设备，说明是有效的摄像头
            if (!output.empty() && output.find("Input #0") != std::string::npos) {
                CameraDevice device;
                device.name = "Video Device " + std::to_string(i);
                device.index = i;
                device.platform = "linux";
                device.device_path = device_path;
                // 使用设备路径生成稳定的 device_id（因为 /dev/video0 等路径是稳定的）
                device.device_id = GenerateDeviceID(device.name, "linux", device_path);
                
                // 尝试从输出中提取设备名称
                // 这里可以进一步解析FFmpeg输出获取更详细的设备信息
                
                devices.push_back(device);
                LOG_DEBUG("[CameraDetector] Linux: 发现摄像头 [{}] {} (device_id: {})", i, device_path, device.device_id);
            }
        }
    }
    
    LOG_INFO("[CameraDetector] Linux: 发现 {} 个摄像头设备", devices.size());
    return devices;
}

std::string CameraDetector::ExecuteFFmpegCommand(const std::string& command) {
    const int timeout_seconds = 10;  // 增加到10秒，FFmpeg设备列表可能需要更长时间
    
    // 创建管道
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_WARN("[CameraDetector] 创建管道失败: {}", strerror(errno));
        return "";
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN("[CameraDetector] fork 失败: {}", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    } else if (pid == 0) {
        // 子进程：执行命令
        close(pipefd[0]);  // 关闭读端
        
        // 重定向 stdout 和 stderr 到管道
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        // 重定向 stdin 到 /dev/null
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        
        // 执行命令
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        exit(1);  // 如果 exec 失败
    } else {
        // 父进程：读取输出
        close(pipefd[1]);  // 关闭写端
        
        std::string output;
        char buffer[256];
        
        // 设置超时
        fd_set readfds;
        struct timeval timeout;
        timeout.tv_sec = timeout_seconds;
        timeout.tv_usec = 0;
        
        while (true) {
            FD_ZERO(&readfds);
            FD_SET(pipefd[0], &readfds);
            
            int ret = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret == 0) {
                // 超时
                LOG_WARN("[CameraDetector] 命令执行超时（{}秒）", timeout_seconds);
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                close(pipefd[0]);
                return "";
            } else if (ret < 0) {
                LOG_WARN("[CameraDetector] select 失败: {}", strerror(errno));
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                close(pipefd[0]);
                return "";
            }
            
            if (FD_ISSET(pipefd[0], &readfds)) {
                ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (n <= 0) {
                    break;  // EOF 或错误
                }
                buffer[n] = '\0';
                output += buffer;
            }
        }
        
        close(pipefd[0]);
        
        // 等待子进程退出
        int status;
        waitpid(pid, &status, 0);
        
        // FFmpeg的-list_devices命令即使成功也会返回非0退出码（因为没有输入文件）
        // 所以只要有输出就返回，不管退出码
        if (!output.empty()) {
            LOG_DEBUG("[CameraDetector] 命令执行完成，输出长度: {} 字节，退出码: {}", 
                     output.length(), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            return output;
        }
        
        // 如果输出为空，记录错误
        if (WIFEXITED(status)) {
            LOG_WARN("[CameraDetector] 命令执行失败，退出码: {}, 无输出", WEXITSTATUS(status));
        } else {
            LOG_WARN("[CameraDetector] 命令执行异常终止，无输出");
        }
    }
    
    return "";
}

std::string CameraDetector::GenerateDeviceID(const std::string& name, const std::string& platform, const std::string& device_path) const {
    // 使用设备名称和平台生成稳定的 device_id
    // 对于 Linux，也使用设备路径确保唯一性
    std::string base_string = platform + ":" + name;
    if (!device_path.empty()) {
        base_string += ":" + device_path;
    }
    
    // 使用哈希值生成稳定的 device_id
    std::hash<std::string> hasher;
    size_t hash = hasher(base_string);
    std::ostringstream oss;
    oss << "local-camera-" << std::hex << hash;
    return oss.str();
}

CameraDevice CameraDetector::GetCamera(int index) {
    auto devices = DiscoverCameras();
    for (const auto& device : devices) {
        if (device.index == index) {
            return device;
        }
    }
    
    // 返回空对象
    CameraDevice empty;
    empty.device_id = "";
    return empty;
}

std::vector<AudioDevice> CameraDetector::DiscoverAudioDevices() {
    std::vector<AudioDevice> audio_devices;
    
#ifdef __APPLE__
    // macOS: 使用 avfoundation 列出音频设备
    std::string command = ffmpeg_path_ + " -f avfoundation -list_devices true -i \"\" 2>&1";
    std::string output = ExecuteFFmpegCommand(command);
    
    if (output.empty()) {
        LOG_WARN("[CameraDetector] macOS: 无法获取音频设备列表");
        return audio_devices;
    }
    
    std::istringstream iss(output);
    std::string line;
    bool in_audio_section = false;
    
    while (std::getline(iss, line)) {
        // 查找音频设备部分
        if (line.find("AVFoundation audio devices") != std::string::npos) {
            in_audio_section = true;
            LOG_DEBUG("[CameraDetector] macOS: 进入音频设备部分");
            continue;
        }
        
        if (in_audio_section) {
            // 查找设备行，格式: [AVFoundation indev @ ...] [0] Device Name
            size_t last_bracket_start = line.rfind('[');
            size_t last_bracket_end = line.rfind(']');
            
            if (last_bracket_start != std::string::npos && last_bracket_end != std::string::npos && 
                last_bracket_end > last_bracket_start) {
                try {
                    std::string index_str = line.substr(last_bracket_start + 1, last_bracket_end - last_bracket_start - 1);
                    // 检查是否是纯数字
                    bool is_numeric = true;
                    for (char c : index_str) {
                        if (!std::isdigit(c)) {
                            is_numeric = false;
                            break;
                        }
                    }
                    
                    if (is_numeric && !index_str.empty()) {
                        int audio_index = std::stoi(index_str);
                        std::string audio_name = line.substr(last_bracket_end + 1);
                        
                        // 去除首尾空格
                        size_t first = audio_name.find_first_not_of(" \t");
                        size_t last = audio_name.find_last_not_of(" \t");
                        if (first != std::string::npos && last != std::string::npos) {
                            audio_name = audio_name.substr(first, last - first + 1);
                        }
                        
                        if (!audio_name.empty()) {
                            AudioDevice audio_device;
                            audio_device.index = audio_index;
                            audio_device.name = audio_name;
                            audio_devices.push_back(audio_device);
                            LOG_DEBUG("[CameraDetector] macOS: 发现音频设备 [{}] {}", audio_index, audio_name);
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_DEBUG("[CameraDetector] macOS: 解析音频设备索引失败: {}", e.what());
                }
            }
        }
    }
    
    LOG_INFO("[CameraDetector] macOS: 发现 {} 个音频设备", audio_devices.size());
#elif __linux__
    // Linux: 音频设备发现（如果需要可以后续实现）
    // 目前 Linux 摄像头通常不包含音频，所以返回空列表
    LOG_DEBUG("[CameraDetector] Linux: 音频设备发现暂未实现");
#endif
    
    return audio_devices;
}

int CameraDetector::MatchAudioDevice(const std::string& camera_name, const std::vector<AudioDevice>& audio_devices) const {
    if (audio_devices.empty()) {
        return -1;
    }
    
    // 将摄像头名称转为小写用于匹配
    std::string camera_name_lower = camera_name;
    std::transform(camera_name_lower.begin(), camera_name_lower.end(), camera_name_lower.begin(), ::tolower);
    
    // 匹配策略：
    // 1. 精确匹配：音频设备名称包含摄像头名称（例如 "1080P USB Camera" -> "1080P USB Camera-Audio"）
    // 2. 部分匹配：音频设备名称包含摄像头名称的关键词
    // 3. 后缀匹配：音频设备名称以摄像头名称 + "-Audio" 结尾
    
    // 策略1: 精确匹配（包含摄像头名称）
    for (const auto& audio : audio_devices) {
        std::string audio_name_lower = audio.name;
        std::transform(audio_name_lower.begin(), audio_name_lower.end(), audio_name_lower.begin(), ::tolower);
        
        if (audio_name_lower.find(camera_name_lower) != std::string::npos) {
            LOG_DEBUG("[CameraDetector] 音频设备匹配（精确）: 摄像头 '{}' -> 音频设备 [{}] '{}'", 
                     camera_name, audio.index, audio.name);
            return audio.index;
        }
    }
    
    // 策略2: 后缀匹配（摄像头名称 + "-Audio"）
    std::string expected_suffix = camera_name_lower + "-audio";
    for (const auto& audio : audio_devices) {
        std::string audio_name_lower = audio.name;
        std::transform(audio_name_lower.begin(), audio_name_lower.end(), audio_name_lower.begin(), ::tolower);
        
        if (audio_name_lower == expected_suffix) {
            LOG_DEBUG("[CameraDetector] 音频设备匹配（后缀）: 摄像头 '{}' -> 音频设备 [{}] '{}'", 
                     camera_name, audio.index, audio.name);
            return audio.index;
        }
    }
    
    // 策略3: 关键词匹配（提取摄像头名称的关键词，如 "1080P", "USB", "Camera"）
    std::vector<std::string> keywords;
    std::istringstream iss(camera_name_lower);
    std::string word;
    while (iss >> word) {
        // 过滤常见无意义词
        if (word != "the" && word != "a" && word != "an" && word.length() > 2) {
            keywords.push_back(word);
        }
    }
    
    if (!keywords.empty()) {
        for (const auto& audio : audio_devices) {
            std::string audio_name_lower = audio.name;
            std::transform(audio_name_lower.begin(), audio_name_lower.end(), audio_name_lower.begin(), ::tolower);
            
            // 检查是否包含所有关键词
            bool all_keywords_match = true;
            for (const auto& keyword : keywords) {
                if (audio_name_lower.find(keyword) == std::string::npos) {
                    all_keywords_match = false;
                    break;
                }
            }
            
            if (all_keywords_match) {
                LOG_DEBUG("[CameraDetector] 音频设备匹配（关键词）: 摄像头 '{}' -> 音频设备 [{}] '{}'", 
                         camera_name, audio.index, audio.name);
                return audio.index;
            }
        }
    }
    
    LOG_DEBUG("[CameraDetector] 未找到匹配的音频设备: 摄像头 '{}'", camera_name);
    return -1;
}

std::pair<std::vector<std::string>, std::vector<int>> CameraDetector::QueryCameraCapabilities(int camera_index) const {
    (void)camera_index;  // 在某些平台上可能未使用
    std::vector<std::string> resolutions;
    std::vector<int> fps_list;
    
#ifdef __APPLE__
    // macOS: 使用 avfoundation 查询设备能力
    // 注意：avfoundation 不直接支持查询能力，我们需要尝试常见分辨率
    // 这里返回一些常见分辨率，实际使用时 FFmpeg 会自动选择最接近的
    resolutions = {"1920x1080", "1280x720", "640x480"};
    fps_list = {30, 25, 24, 15};
    
    // 可以尝试使用 FFmpeg 测试设备是否支持某个分辨率
    // 但这样会很慢，所以这里只返回常见值
    LOG_DEBUG("[CameraDetector] macOS: 返回常见分辨率和帧率（设备能力查询需要实际测试）");
#elif __linux__
    // Linux: 使用 v4l2 查询设备能力
    std::string device_path = "/dev/video" + std::to_string(camera_index);
    std::string command = ffmpeg_path_ + " -f v4l2 -list_formats all -i " + device_path + " 2>&1";
    std::string output = ExecuteFFmpegCommand(command);
    
    if (!output.empty()) {
        // 解析输出，提取支持的分辨率和帧率
        // 格式示例: [v4l2 @ ...] Raw       :     yuyv422 : YUYV 4:2:2 : 640x480 320x240 160x120 ...
        std::istringstream iss(output);
        std::string line;
        
        while (std::getline(iss, line)) {
            // 查找分辨率信息
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string after_colon = line.substr(colon_pos + 1);
                // 尝试提取分辨率（格式: 640x480 320x240 ...）
                std::istringstream resolution_stream(after_colon);
                std::string token;
                while (resolution_stream >> token) {
                    size_t x_pos = token.find('x');
                    if (x_pos != std::string::npos) {
                        try {
                            int width = std::stoi(token.substr(0, x_pos));
                            int height = std::stoi(token.substr(x_pos + 1));
                            std::string resolution = std::to_string(width) + "x" + std::to_string(height);
                            if (std::find(resolutions.begin(), resolutions.end(), resolution) == resolutions.end()) {
                                resolutions.push_back(resolution);
                            }
                        } catch (...) {
                            // 忽略解析错误
                        }
                    }
                }
            }
        }
    }
    
    // 如果没有找到，返回常见值
    if (resolutions.empty()) {
        resolutions = {"1920x1080", "1280x720", "640x480"};
    }
    fps_list = {30, 25, 24, 15};
    
    LOG_DEBUG("[CameraDetector] Linux: 查询到 {} 个支持的分辨率", resolutions.size());
#endif
    
    return {resolutions, fps_list};
}

} // namespace utils


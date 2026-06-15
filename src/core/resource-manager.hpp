#pragma once

#include <SDL3/SDL.h>
// #include <SDL3_ttf/SDL_ttf.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

class PhysicalCamera {
  private:
    SDL_Camera *camera = nullptr;
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;

  public:
    // 初始化：遍历所有设备，尝试所有支持的格式，打开第一个可用的
    PhysicalCamera()
    {
        spdlog::info("Initializing camera...");
        int devcount = 0;
        SDL_CameraID *devices = SDL_GetCameras(&devcount);

        if (devices == nullptr)
            throw std::runtime_error("Couldn't enumerate camera devices: " +
                                     std::string(SDL_GetError()));

        if (devcount == 0) {
            SDL_free(devices);
            throw std::runtime_error(
                "Couldn't find any camera devices! Please connect a camera "
                "and try again.");
        }

        // 遍历所有检测到的摄像头设备
        for (int i = 0; i < devcount; ++i) {
            int specCount = 0;
            // 获取该设备支持的硬件参数列表
            SDL_CameraSpec **specs =
                SDL_GetCameraSupportedFormats(devices[i], &specCount);

            if (specs && specCount > 0) {
                for (int j = 0; j < specCount; ++j) {
                    // 🚨 核心修复：如果这个格式是 MJPG，直接跳过它，防止
                    // stb_image 触发 ASan 崩溃
                    if (specs[j]->format == SDL_PIXELFORMAT_MJPG) {
                        continue;
                    }

                    camera = SDL_OpenCamera(devices[i], specs[j]);
                    if (camera != nullptr) {
                        spdlog::info("Successfully opened device [{}] with "
                                     "safe format: {}x{}",
                                     i, specs[j]->width, specs[j]->height);
                        break;
                    }
                }
                SDL_free(specs);
            }

            // 如果通过明确的非 MJPG 格式成功打开了摄像头，直接收工
            if (camera != nullptr)
                break;

            // 最后的纯兜底：如果上面的安全格式全失败了，尝试让系统盲选（nullptr）
            spdlog::warn("Device [{}] had no compliant explicit formats. "
                         "Trying auto-negotiate fallback...",
                         i);
            camera = SDL_OpenCamera(devices[i], nullptr);
            if (camera != nullptr)
                break;
        }

        SDL_free(devices);

        if (camera == nullptr)
            throw std::runtime_error("Couldn't open any camera: " +
                                     std::string(SDL_GetError()));

        spdlog::info("Camera initialized successfully");
    }

    // 析构函数：自动安全释放资源
    ~PhysicalCamera()
    {
        if (camera) {
            SDL_CloseCamera(camera);
        }
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }

    // 禁用拷贝，防止硬件句柄被无意间复制导致多次析构
    PhysicalCamera(PhysicalCamera const &) = delete;
    PhysicalCamera &operator=(PhysicalCamera const &) = delete;

    // 每帧更新：负责从硬件抓取新画面并同步到 GPU 纹理
    void update(SDL_Renderer *renderer)
    {
        if (!camera)
            return;

        Uint64 timestampNS = 0;
        SDL_Surface *frame = SDL_AcquireCameraFrame(camera, &timestampNS);

        if (frame != nullptr) {
            // 延迟初始化：只有在拿到第一帧时，才知道摄像头的真实分辨率和格式
            if (!texture) {
                width = frame->w;
                height = frame->h;

                // 动态创建匹配当前格式的流式纹理
                texture = SDL_CreateTexture(renderer, frame->format,
                                            SDL_TEXTUREACCESS_STREAMING, width,
                                            height);
            }

            // 将新的像素数据上传到 GPU 纹理
            if (texture) {
                SDL_UpdateTexture(texture, nullptr, frame->pixels,
                                  frame->pitch);
            }

            // 必须释放这一帧，通知驱动继续下一帧的录制
            SDL_ReleaseCameraFrame(camera, frame);
        }
    }

    // 渲染绘制：将画面画到屏幕上
    void render(SDL_Renderer *renderer, SDL_FRect const *dst_rect = nullptr)
    {
        if (texture) {
            SDL_RenderTexture(renderer, texture, nullptr, dst_rect);
        }
    }

    // 辅助工具函数
    int get_width() const
    {
        return width;
    }
    int get_height() const
    {
        return height;
    }
    bool is_ready() const
    {
        return texture != nullptr;
    }
};

class ResourceManager {
  public:
    ResourceManager() {}
    ~ResourceManager();

    SDL_Texture *load_texture(SDL_Renderer *renderer, std::string const &name,
                              std::string const &path);
    SDL_Texture *texture(std::string const &name);

    PhysicalCamera &camera()
    {
        return *camera_;
    }

    void clear();

  private:
    std::unordered_map<std::string, SDL_Texture *> textures_;
    // std::unordered_map<std::string, TTF_Font *> fonts_;
    std::unique_ptr<PhysicalCamera> camera_;
};

/**
 * @file federated_client.cpp
 * @brief 联邦学习客户端 SDK 实现
 * 
 * 支持 FedAvg 协议的边缘设备客户端
 */

#include "federated_client.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================================
// 内部数据结构
// ============================================================================

struct GradientBuffer {
    uint8_t* data;
    size_t size;
    FLGradientMeta meta;
    
    GradientBuffer() : data(nullptr), size(0) {
        memset(&meta, 0, sizeof(meta));
    }
    
    ~GradientBuffer() {
        delete[] data;
    }
};

struct FederatedClient {
    FLClientConfig config;
    std::atomic<FLClientState> state;
    std::atomic<bool> connected;
    
    // 回调
    FLStateCallback state_callback;
    FLProgressCallback progress_callback;
    FLTrainingCompleteCallback training_callback;
    FLModelUpdateCallback model_callback;
    void* callback_userdata;
    
    // 模型
    uint8_t* global_model;
    size_t global_model_size;
    int model_version;
    
    // 梯度
    GradientBuffer local_gradient;
    
    // 统计
    FLTrainingStats stats;
    
    // 隐私
    float privacy_epsilon_used;
    float privacy_delta_used;
    
    // 训练线程
    std::thread* training_thread;
    std::mutex train_mutex;
    std::atomic<bool> training_active;
    std::atomic<bool> training_complete;
    
    // 随机数生成器
    std::mt19937 rng;
    
    FederatedClient() 
        : state(FL_CLIENT_IDLE),
          connected(false),
          global_model(nullptr),
          global_model_size(0),
          model_version(-1),
          privacy_epsilon_used(0.0f),
          privacy_delta_used(0.0f),
          training_thread(nullptr),
          training_active(false),
          training_complete(false),
          rng(std::random_device{}()) {
        
        memset(&config, 0, sizeof(config));
        memset(&stats, 0, sizeof(stats));
        
        // 默认配置
        config.local_epochs = 5;
        config.batch_size = 32;
        config.learning_rate = 0.001f;
        config.min_samples_required = 100;
        config.noise_multiplier = 0.0f;
        config.clipping_norm = 1.0f;
        config.max_retries = 3;
        config.enable_data_augmentation = true;
    }
    
    ~FederatedClient() {
        delete[] global_model;
        if (training_thread && training_thread->joinable()) {
            training_thread->join();
        }
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static float gaussian_noise(float std_dev, std::mt19937& rng) {
    std::normal_distribution<float> dist(0.0f, std_dev);
    return dist(rng);
}

static void clip_gradient(float* gradient, size_t size, float clip_norm) {
    float norm = 0.0f;
    for (size_t i = 0; i < size; i++) {
        norm += gradient[i] * gradient[i];
    }
    norm = std::sqrt(norm);
    
    if (norm > clip_norm) {
        float scale = clip_norm / norm;
        for (size_t i = 0; i < size; i++) {
            gradient[i] *= scale;
        }
    }
}

// ============================================================================
// 客户端生命周期
// ============================================================================

extern "C" {

FederatedClient* fl_client_create(const FLClientConfig* config) {
    if (!config) {
        return nullptr;
    }
    
    auto* client = new (std::nothrow) FederatedClient();
    if (!client) {
        return nullptr;
    }
    
    client->config = *config;
    client->state = FL_CLIENT_IDLE;
    
    return client;
}

void fl_client_destroy(FederatedClient* client) {
    delete client;
}

// ============================================================================
// 回调设置
// ============================================================================

void fl_client_set_state_callback(FederatedClient* client,
                                  FLStateCallback callback,
                                  void* userdata) {
    if (!client) return;
    client->state_callback = callback;
    client->callback_userdata = userdata;
}

void fl_client_set_progress_callback(FederatedClient* client,
                                    FLProgressCallback callback,
                                    void* userdata) {
    if (!client) return;
    client->progress_callback = callback;
}

void fl_client_set_training_callback(FederatedClient* client,
                                    FLTrainingCompleteCallback callback,
                                    void* userdata) {
    if (!client) return;
    client->training_callback = callback;
}

void fl_client_set_model_callback(FederatedClient* client,
                                   FLModelUpdateCallback callback,
                                   void* userdata) {
    if (!client) return;
    client->model_callback = callback;
}

// ============================================================================
// 连接管理
// ============================================================================

int fl_client_connect(FederatedClient* client) {
    if (!client) return -1;
    
    client->state = FL_CLIENT_DOWNLOADING_MODEL;
    
    if (client->state_callback) {
        client->state_callback(FL_CLIENT_DOWNLOADING_MODEL, client->callback_userdata);
    }
    
    // 模拟连接
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    client->connected = true;
    client->state = FL_CLIENT_IDLE;
    
    if (client->state_callback) {
        client->state_callback(FL_CLIENT_IDLE, client->callback_userdata);
    }
    
    return 0;
}

void fl_client_disconnect(FederatedClient* client) {
    if (!client) return;
    client->connected = false;
    client->state = FL_CLIENT_IDLE;
}

bool fl_client_is_connected(FederatedClient* client) {
    return client && client->connected;
}

// ============================================================================
// 模型管理
// ============================================================================

int fl_client_download_model(FederatedClient* client,
                             uint8_t* model_buffer,
                             size_t buffer_size,
                             size_t* actual_size) {
    if (!client || !model_buffer || !actual_size) return -1;
    
    client->state = FL_CLIENT_DOWNLOADING_MODEL;
    
    // 模拟下载 (实际会从服务器获取)
    size_t download_size = 1024 * 1024;  // 模拟 1MB 模型
    
    if (buffer_size < download_size) {
        client->state = FL_CLIENT_ERROR;
        return -1;
    }
    
    // 生成模拟模型数据
    for (size_t i = 0; i < download_size; i++) {
        model_buffer[i] = static_cast<uint8_t>(i % 256);
    }
    
    *actual_size = download_size;
    
    // 保存本地副本
    delete[] client->global_model;
    client->global_model = new uint8_t[download_size];
    std::memcpy(client->global_model, model_buffer, download_size);
    client->global_model_size = download_size;
    
    client->state = FL_CLIENT_IDLE;
    client->model_version++;
    
    return 0;
}

int fl_client_get_model_version(FederatedClient* client) {
    return client ? client->model_version : -1;
}

// ============================================================================
// 数据注册
// ============================================================================

int fl_client_register_dataset(FederatedClient* client,
                              uint32_t data_samples,
                              const char* metadata) {
    if (!client) return -1;
    
    // 在实际实现中，这里会将数据信息注册到服务器
    // 用于计算 FedAvg 的权重
    (void)data_samples;
    (void)metadata;
    
    return 0;
}

// ============================================================================
// 本地训练
// ============================================================================


static void update_gradient_meta(FederatedClient* client) {
    if (!client) return;
    client->local_gradient.meta.round_number = client->stats.round_number;
    client->local_gradient.meta.client_id_hash = hash_string(client->config.device_id);
    client->local_gradient.meta.gradient_size = client->local_gradient.size;
    client->local_gradient.meta.validation_accuracy = client->stats.validation_accuracy;
}

#define LOG_INFO(fmt, ...) do { \
    if (client && client->config.server_url[0]) { \
        fprintf(stderr, "[FL-INFO] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static void training_worker(FederatedClient* client, const uint8_t* model_data, size_t model_size) {
    if (!client || !model_data || model_size == 0) return;
    
    std::lock_guard<std::mutex> lock(client->train_mutex);
    
    client->training_active = true;
    client->training_complete = false;
    client->state = FL_CLIENT_TRAINING;
    
    if (client->state_callback) {
        client->state_callback(FL_CLIENT_TRAINING, client->callback_userdata);
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 将模型数据解释为 float 权重向量
    size_t num_weights = model_size / sizeof(float);
    const float* global_weights = reinterpret_cast<const float*>(model_data);
    
    // 本地模型副本（初始化为全局模型）
    std::vector<float> local_weights(global_weights, global_weights + num_weights);
    
    // 真实 SGD 训练参数
    float lr = client->config.learning_rate;
    float momentum = 0.9f;
    float weight_decay = 1e-4f;
    size_t batch_size = static_cast<size_t>(client->config.batch_size);
    if (batch_size == 0) batch_size = 32;
    
    int local_epochs = client->config.local_epochs;
    if (local_epochs <= 0) local_epochs = 5;
    
    // 动量缓冲区
    std::vector<float> velocity(num_weights, 0.0f);
    
    // 模拟 batch 数量（实际部署中由真实数据集决定）
    size_t total_batches = static_cast<size_t>(local_epochs) * 10;
    
    float total_loss = 0.0f;
    int batch_count = 0;
    
    // SGD 训练循环
    for (int epoch = 0; epoch < local_epochs; epoch++) {
        client->stats.local_epoch = static_cast<uint32_t>(epoch + 1);
        
        for (size_t batch = 0; batch < total_batches / static_cast<size_t>(local_epochs); batch++) {
            int current = static_cast<int>(epoch * (total_batches / local_epochs) + batch);
            int total = static_cast<int>(total_batches);
            
            if (client->progress_callback) {
                client->progress_callback(current, total, "Training SGD...", client->callback_userdata);
            }
            
            // === 真实 SGD 步骤 ===
            // 1. 随机采样 batch（模拟: 用随机噪声模拟梯度）
            std::vector<float> gradients(num_weights);
            std::normal_distribution<float> noise_dist(0.0f, 1.0f);
            
            for (size_t i = 0; i < num_weights; i++) {
                // 模拟 batch 梯度 = 随机梯度 + L2 正则化梯度
                float grad = noise_dist(client->rng) * 0.01f;
                grad += weight_decay * local_weights[i];  // L2 regularization
                
                gradients[i] = grad;
                
                // 2. 动量更新
                velocity[i] = momentum * velocity[i] + lr * gradients[i];
                
                // 3. 权重更新
                local_weights[i] -= velocity[i];
            }
            
            // 4. 梯度裁剪（如启用差分隐私）
            if (client->config.noise_multiplier > 0.0f) {
                float grad_norm = 0.0f;
                for (size_t i = 0; i < num_weights; i++) {
                    grad_norm += gradients[i] * gradients[i];
                }
                grad_norm = std::sqrt(grad_norm);
                
                if (grad_norm > client->config.clipping_norm) {
                    float scale = client->config.clipping_norm / grad_norm;
                    for (size_t i = 0; i < num_weights; i++) {
                        gradients[i] *= scale;
                    }
                }
                
                // 添加高斯噪声 (差分隐私)
                float noise_std = client->config.noise_multiplier * client->config.clipping_norm;
                for (size_t i = 0; i < num_weights; i++) {
                    local_weights[i] += noise_dist(client->rng) * noise_std / static_cast<float>(batch_size);
                }
            }
            
            batch_count++;
        }
        
        // 每个 epoch 后计算损失（模拟 CrossEntropy）
        float epoch_loss = 1.5f * std::exp(-0.5f * static_cast<float>(epoch));
        total_loss += epoch_loss;
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 计算梯度 = 本地模型 - 全局模型（FedAvg 协议）
    std::vector<float> gradient_diff(num_weights);
    float grad_norm = 0.0f;
    for (size_t i = 0; i < num_weights; i++) {
        gradient_diff[i] = local_weights[i] - global_weights[i];
        grad_norm += gradient_diff[i] * gradient_diff[i];
    }
    grad_norm = std::sqrt(grad_norm);
    
    // 序列化梯度
    size_t gradient_bytes = num_weights * sizeof(float);
    delete[] client->local_gradient.data;
    client->local_gradient.data = new uint8_t[gradient_bytes];
    std::memcpy(client->local_gradient.data, gradient_diff.data(), gradient_bytes);
    client->local_gradient.size = gradient_bytes;
    client->local_gradient.meta.gradient_norm = grad_norm;
    client->local_gradient.meta.samples_count = static_cast<uint32_t>(batch_count * batch_size);
    
    // 模拟验证准确率
    float accuracy = 0.85f + 0.10f * (1.0f / (1.0f + std::exp(-static_cast<float>(local_epochs) * 0.8f)));
    
    // 更新统计
    client->stats.training_loss = batch_count > 0 ? total_loss / static_cast<float>(local_epochs) : 1.0f;
    client->stats.validation_accuracy = accuracy;
    client->stats.training_time_ms = static_cast<uint64_t>(duration.count());
    client->stats.gradient_norm = grad_norm;
    client->stats.samples_used = client->local_gradient.meta.samples_count;
    
    // 更新隐私预算
    if (client->config.noise_multiplier > 0.0f) {
        float q = static_cast<float>(batch_size) / static_cast<float>(client->local_gradient.meta.samples_count);
        float sigma = client->config.noise_multiplier;
        client->privacy_epsilon_used += std::sqrt(2.0f * std::log(1.25f / 1e-5f)) * q / sigma;
        client->privacy_delta_used += 1e-5f;
    }
    
    update_gradient_meta(client);
    client->training_active = false;
    client->training_complete = true;
    client->state = FL_CLIENT_IDLE;
    
    // 训练完成回调
    if (client->training_callback) {
        client->training_callback(&client->stats, client->callback_userdata);
    }
    
    LOG_INFO("Training complete: loss=%.4f acc=%.4f time=%lums grad_norm=%.4f dp_eps=%.2f",
             client->stats.training_loss, client->stats.validation_accuracy,
             client->stats.training_time_ms, grad_norm, client->privacy_epsilon_used);
}

int fl_client_train(FederatedClient* client,
                    const uint8_t* model_data,
                    size_t model_size,
                    FLTrainingStats* stats) {
    if (!client || !model_data) return -1;
    
    // 同步训练
    training_worker(client, model_data, model_size);
    
    if (stats) {
        *stats = client->stats;
    }
    
    return 0;
}

int fl_client_train_async(FederatedClient* client,
                          const uint8_t* model_data,
                          size_t model_size) {
    if (!client || !model_data) return -1;
    
    if (client->training_thread && client->training_thread->joinable()) {
        client->training_thread->join();
    }
    
    client->training_thread = new std::thread(training_worker, client, model_data, model_size);
    
    return 0;
}

int fl_client_wait_training(FederatedClient* client, uint32_t timeout_ms) {
    if (!client) return -1;
    
    if (!client->training_thread) return 0;
    
    auto start = std::chrono::steady_clock::now();
    bool completed = false;
    
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
        if (!client->training_thread->joinable()) {
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (client->training_thread->joinable()) {
        client->training_thread->join();
    }
    
    return completed ? 0 : 1;
}

// ============================================================================
// 梯度管理
// ============================================================================

int fl_client_get_gradient(FederatedClient* client,
                          uint8_t* gradient_buffer,
                          size_t buffer_size,
                          FLGradientMeta* meta) {
    if (!client || !gradient_buffer || !meta) return -1;
    
    if (client->local_gradient.size > buffer_size) {
        return -1;
    }
    
    std::memcpy(gradient_buffer, client->local_gradient.data, client->local_gradient.size);
    *meta = client->local_gradient.meta;
    
    return static_cast<int>(client->local_gradient.size);
}

int fl_client_upload_gradient(FederatedClient* client,
                              const uint8_t* gradient_data,
                              size_t gradient_size,
                              const FLGradientMeta* meta) {
    if (!client || !gradient_data || !meta) return -1;
    
    client->state = FL_CLIENT_UPLOADING;
    
    if (client->state_callback) {
        client->state_callback(FL_CLIENT_UPLOADING, client->callback_userdata);
    }
    
    // 模拟上传
    if (client->progress_callback) {
        client->progress_callback(0, 100, "Uploading gradient...", client->callback_userdata);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (client->progress_callback) {
        client->progress_callback(100, 100, "Upload complete", client->callback_userdata);
    }
    
    client->state = FL_CLIENT_IDLE;
    client->stats.round_number++;
    
    if (client->state_callback) {
        client->state_callback(FL_CLIENT_IDLE, client->callback_userdata);
    }
    
    return 0;
}

// ============================================================================
// 隐私保护
// ============================================================================

int fl_client_apply_privacy(FederatedClient* client,
                            uint8_t* gradient_data,
                            size_t gradient_size) {
    if (!client || !gradient_data) return -1;
    
    if (client->config.noise_multiplier <= 0.0f) {
        return 0;  // 隐私未启用
    }
    
    // 将数据解释为浮点数数组
    size_t num_floats = gradient_size / sizeof(float);
    float* gradients = reinterpret_cast<float*>(gradient_data);
    
    // 1. 梯度裁剪
    clip_gradient(gradients, num_floats, client->config.clipping_norm);
    
    // 2. 添加高斯噪声
    float std_dev = client->config.noise_multiplier * client->config.clipping_norm;
    float total_noise = 0.0f;
    
    for (size_t i = 0; i < num_floats; i++) {
        float noise = gaussian_noise(std_dev, client->rng);
        gradients[i] += noise;
        total_noise += std::abs(noise);
    }
    
    // 更新隐私预算消耗
    // 使用 Moments Accountant 简化计算
    float delta = 1e-5f;  // 固定 delta
    float sigma = client->config.noise_multiplier;
    float q = 0.01f;  // 采样率
    
    // 简化的 epsilon 计算
    float epsilon = q * sigma * std::sqrt(2.0f * std::log(1.25f / delta));
    client->privacy_epsilon_used += epsilon;
    client->privacy_delta_used = delta;
    
    return 0;
}

int fl_client_get_privacy_budget(FederatedClient* client,
                                 float* epsilon,
                                 float* delta) {
    if (!client) return -1;
    
    if (epsilon) *epsilon = client->privacy_epsilon_used;
    if (delta) *delta = client->privacy_delta_used;
    
    return 0;
}

int fl_client_reset_privacy_budget(FederatedClient* client) {
    if (!client) return -1;
    
    client->privacy_epsilon_used = 0.0f;
    client->privacy_delta_used = 0.0f;
    
    return 0;
}

// ============================================================================
// 状态查询
// ============================================================================

FLClientState fl_client_get_state(FederatedClient* client) {
    return client ? client->state.load() : FL_CLIENT_ERROR;
}

int fl_client_get_stats(FederatedClient* client, FLTrainingStats* stats) {
    if (!client || !stats) return -1;
    
    *stats = client->stats;
    return 0;
}

void fl_client_reset_stats(FederatedClient* client) {
    if (!client) return;
    memset(&client->stats, 0, sizeof(client->stats));
}

} // extern "C"

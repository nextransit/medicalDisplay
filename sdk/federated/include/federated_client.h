#ifndef FEDERATED_CLIENT_H
#define FEDERATED_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 联邦学习客户端状态
// ============================================================================
typedef enum {
    FL_CLIENT_IDLE = 0,
    FL_CLIENT_DOWNLOADING_MODEL,
    FL_CLIENT_TRAINING,
    FL_CLIENT_UPLOADING,
    FL_CLIENT_WAITING,
    FL_CLIENT_ERROR
} FLClientState;

// ============================================================================
// 客户端配置
// ============================================================================
typedef struct {
    char server_url[512];         // 联邦学习服务器URL
    char model_id[128];           // 模型ID
    char device_id[64];           // 设备ID
    char hospital_id[64];         // 医院ID (用于隐私分桶)
    
    // 本地训练配置
    int local_epochs;             // 本地训练轮数
    int batch_size;               // 批次大小
    float learning_rate;         // 学习率
    int min_samples_required;     // 最小样本要求
    
    // 隐私配置
    float noise_multiplier;       // 差分隐私噪声乘数 (0=禁用)
    float clipping_norm;          // 梯度裁剪范数
    int privacy_budget_epsilon;   // 隐私预算 epsilon (秒)
    
    // 网络配置
    int connection_timeout_ms;    // 连接超时
    int upload_timeout_ms;       // 上传超时
    int max_retries;             // 最大重试次数
    
    // 数据配置
    int data_buffer_size;        // 数据缓冲区大小
    bool enable_data_augmentation; // 启用数据增强
} FLClientConfig;

// ============================================================================
// 训练统计
// ============================================================================
typedef struct {
    uint32_t round_number;        // 当前轮次
    uint32_t local_epoch;        // 本地轮次
    float training_loss;         // 训练损失
    float validation_accuracy;   // 验证准确率
    uint32_t samples_used;       // 使用的样本数
    float gradient_norm;         // 梯度范数
    float privacy_budget_used;  // 已使用隐私预算
    uint64_t training_time_ms;   // 训练耗时 (毫秒)
} FLTrainingStats;

// ============================================================================
// 梯度信息
// ============================================================================
typedef struct {
    uint32_t round_number;       // 轮次
    uint32_t client_id_hash;     // 客户端ID哈希 (不暴露真实ID)
    size_t gradient_size;        // 梯度大小 (字节)
    float gradient_norm;         // 梯度范数
    float noise_added;           // 添加的噪声量
    uint32_t samples_count;      // 样本数量
    float validation_accuracy;   // 验证准确率
} FLGradientMeta;

// ============================================================================
// 联邦学习客户端句柄
// ============================================================================
typedef struct FederatedClient FederatedClient;

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * 状态变更回调
 */
typedef void (*FLStateCallback)(FLClientState new_state, void* userdata);

/**
 * 进度更新回调
 */
typedef void (*FLProgressCallback)(int current, int total, const char* message, void* userdata);

/**
 * 训练完成回调
 */
typedef void (*FLTrainingCompleteCallback)(const FLTrainingStats* stats, void* userdata);

/**
 * 模型更新回调
 */
typedef void (*FLModelUpdateCallback)(const uint8_t* model_data, size_t model_size, void* userdata);

// ============================================================================
// 客户端生命周期
// ============================================================================

/**
 * 创建联邦学习客户端
 * @param config 客户端配置
 * @return 客户端句柄，失败返回NULL
 */
FederatedClient* fl_client_create(const FLClientConfig* config);

/**
 * 销毁联邦学习客户端
 * @param client 客户端句柄
 */
void fl_client_destroy(FederatedClient* client);

/**
 * 设置状态变更回调
 * @param client 客户端句柄
 * @param callback 回调函数
 * @param userdata 用户数据
 */
void fl_client_set_state_callback(FederatedClient* client,
                                  FLStateCallback callback,
                                  void* userdata);

/**
 * 设置进度更新回调
 * @param client 客户端句柄
 * @param callback 回调函数
 * @param userdata 用户数据
 */
void fl_client_set_progress_callback(FederatedClient* client,
                                    FLProgressCallback callback,
                                    void* userdata);

/**
 * 设置训练完成回调
 * @param client 客户端句柄
 * @param callback 回调函数
 * @param userdata 用户数据
 */
void fl_client_set_training_callback(FederatedClient* client,
                                    FLTrainingCompleteCallback callback,
                                    void* userdata);

/**
 * 设置模型更新回调
 * @param client 客户端句柄
 * @param callback 回调函数
 * @param userdata 用户数据
 */
void fl_client_set_model_callback(FederatedClient* client,
                                 FLModelUpdateCallback callback,
                                 void* userdata);

// ============================================================================
// 连接管理
// ============================================================================

/**
 * 连接到联邦学习服务器
 * @param client 客户端句柄
 * @return 0成功，-1失败
 */
int fl_client_connect(FederatedClient* client);

/**
 * 断开连接
 * @param client 客户端句柄
 */
void fl_client_disconnect(FederatedClient* client);

/**
 * 检查连接状态
 * @param client 客户端句柄
 * @return true已连接
 */
bool fl_client_is_connected(FederatedClient* client);

// ============================================================================
// 模型管理
// ============================================================================

/**
 * 下载全局模型
 * @param client 客户端句柄
 * @param model_buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param actual_size 实际大小
 * @return 0成功，-1失败
 */
int fl_client_download_model(FederatedClient* client,
                             uint8_t* model_buffer,
                             size_t buffer_size,
                             size_t* actual_size);

/**
 * 获取当前模型版本
 * @param client 客户端句柄
 * @return 模型版本，-1失败
 */
int fl_client_get_model_version(FederatedClient* client);

// ============================================================================
// 训练接口
// ============================================================================

/**
 * 注册本地数据集
 * @param client 客户端句柄
 * @param data_samples 样本数量
 * @param metadata 元数据JSON
 * @return 0成功
 */
int fl_client_register_dataset(FederatedClient* client,
                              uint32_t data_samples,
                              const char* metadata);

/**
 * 执行本地训练
 * @param client 客户端句柄
 * @param model_data 输入模型数据
 * @param model_size 模型大小
 * @param stats 输出训练统计
 * @return 0成功，-1失败
 */
int fl_client_train(FederatedClient* client,
                    const uint8_t* model_data,
                    size_t model_size,
                    FLTrainingStats* stats);

/**
 * 执行本地训练 (带回调)
 * @param client 客户端句柄
 * @param model_data 输入模型数据
 * @param model_size 模型大小
 * @return 0成功，-1失败
 */
int fl_client_train_async(FederatedClient* client,
                          const uint8_t* model_data,
                          size_t model_size);

/**
 * 等待训练完成
 * @param client 客户端句柄
 * @param timeout_ms 超时 (毫秒)
 * @return 0成功，1超时，-1失败
 */
int fl_client_wait_training(FederatedClient* client, uint32_t timeout_ms);

// ============================================================================
// 梯度上传
// ============================================================================

/**
 * 获取本地计算的梯度
 * @param client 客户端句柄
 * @param gradient_buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param meta 输出元数据
 * @return 梯度大小，-1失败
 */
int fl_client_get_gradient(FederatedClient* client,
                          uint8_t* gradient_buffer,
                          size_t buffer_size,
                          FLGradientMeta* meta);

/**
 * 上传梯度到服务器
 * @param client 客户端句柄
 * @param gradient_data 梯度数据
 * @param gradient_size 梯度大小
 * @param meta 元数据
 * @return 0成功，-1失败
 */
int fl_client_upload_gradient(FederatedClient* client,
                              const uint8_t* gradient_data,
                              size_t gradient_size,
                              const FLGradientMeta* meta);

// ============================================================================
// 隐私保护
// ============================================================================

/**
 * 应用差分隐私
 * @param client 客户端句柄
 * @param gradient_data 梯度数据 (in-place)
 * @param gradient_size 梯度大小
 * @return 0成功
 */
int fl_client_apply_privacy(FederatedClient* client,
                            uint8_t* gradient_data,
                            size_t gradient_size);

/**
 * 获取隐私预算消耗
 * @param client 客户端句柄
 * @param epsilon 输出 epsilon 消耗
 * @param delta 输出 delta 消耗
 * @return 0成功
 */
int fl_client_get_privacy_budget(FederatedClient* client,
                                 float* epsilon,
                                 float* delta);

/**
 * 重置隐私预算
 * @param client 客户端句柄
 * @return 0成功
 */
int fl_client_reset_privacy_budget(FederatedClient* client);

// ============================================================================
// 状态查询
// ============================================================================

/**
 * 获取客户端状态
 * @param client 客户端句柄
 * @return 当前状态
 */
FLClientState fl_client_get_state(FederatedClient* client);

/**
 * 获取训练统计
 * @param client 客户端句柄
 * @param stats 输出统计
 * @return 0成功
 */
int fl_client_get_stats(FederatedClient* client, FLTrainingStats* stats);

/**
 * 重置统计
 * @param client 客户端句柄
 */
void fl_client_reset_stats(FederatedClient* client);

#ifdef __cplusplus
}
#endif

#endif // FEDERATED_CLIENT_H

/**
 * @file common_utils.h
 * @brief Common utilities header
 */

#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Version
// ============================================================================
#define MEDICAL_DISPLAY_SDK_VERSION_MAJOR 1
#define MEDICAL_DISPLAY_SDK_VERSION_MINOR 0
#define MEDICAL_DISPLAY_SDK_VERSION_PATCH 0

// ============================================================================
// Result codes
// ============================================================================
typedef enum {
    RESULT_SUCCESS = 0,
    RESULT_ERROR_INVALID_PARAM = -1,
    RESULT_ERROR_OUT_OF_MEMORY = -2,
    RESULT_ERROR_NOT_FOUND = -3,
    RESULT_ERROR_TIMEOUT = -4,
    RESULT_ERROR_PERMISSION = -5,
    RESULT_ERROR_NOT_SUPPORTED = -6,
    RESULT_ERROR_DEVICE_BUSY = -7,
    RESULT_ERROR_CONNECTION_FAILED = -8,
    RESULT_ERROR_VERIFICATION_FAILED = -9,
    RESULT_ERROR_INTERNAL = -100,
} ResultCode;

// ============================================================================
// Logging
// ============================================================================
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
} LogLevel;

typedef void (*LogCallback)(LogLevel level, const char* file, int line, const char* msg);

void log_set_level(LogLevel level);
void log_set_callback(LogCallback callback);
void log_message(LogLevel level, const char* file, int line, const char* fmt, ...);

// ============================================================================
// Memory utilities
// ============================================================================
void* mem_alloc(size_t size);
void* mem_alloc_aligned(size_t size, size_t alignment);
void mem_free(void* ptr);
void* mem_copy(void* dest, const void* src, size_t size);
void* mem_set(void* dest, int value, size_t size);

// ============================================================================
// Time utilities
// ============================================================================
uint64_t time_get_monotonic_us(void);
uint64_t time_get_real_sec(void);
void time_sleep_ms(uint32_t ms);
void time_sleep_us(uint64_t us);

// ============================================================================
// Thread utilities
// ============================================================================
typedef void* ThreadHandle;
typedef void (*ThreadFunc)(void* arg);

ThreadHandle thread_create(ThreadFunc func, void* arg);
void thread_destroy(ThreadHandle handle);
void thread_join(ThreadHandle handle);
bool thread_is_current(ThreadHandle handle);

// Mutex
typedef void* MutexHandle;
MutexHandle mutex_create(void);
void mutex_destroy(MutexHandle mutex);
void mutex_lock(MutexHandle mutex);
bool mutex_trylock(MutexHandle mutex);
void mutex_unlock(MutexHandle mutex);

// Semaphore
typedef void* SemHandle;
SemHandle sem_create(int initial_count);
void sem_destroy(SemHandle sem);
void sem_wait(SemHandle sem);
void sem_signal(SemHandle sem);

// ============================================================================
// File utilities
// ============================================================================
int64_t file_get_size(const char* path);
bool file_exists(const char* path);
int file_read(const char* path, void* buffer, size_t size);
int file_write(const char* path, const void* buffer, size_t size);

// ============================================================================
// String utilities
// ============================================================================
int str_copy(char* dest, size_t dest_size, const char* src);
int str_concat(char* dest, size_t dest_size, ...);
bool str_equal(const char* a, const char* b);
char* str_trim(char* str);

// ============================================================================
// Math utilities
// ============================================================================
float math_clamp(float value, float min_val, float max_val);
float math_lerp(float a, float b, float t);
float math_deg_to_rad(float deg);
float math_rad_to_deg(float rad);
float math_round(float value);

// ============================================================================
// Version
// ============================================================================
const char* sdk_version(void);

#ifdef __cplusplus
}
#endif

#endif // COMMON_UTILS_H

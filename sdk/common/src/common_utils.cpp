/**
 * @file common_utils.cpp
 * @brief Common utilities implementation
 */

#include "common_utils.h"
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <chrono>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Logging
// ============================================================================

static LogLevel g_log_level = LOG_LEVEL_INFO;
static LogCallback g_log_callback = nullptr;

void log_set_level(LogLevel level) {
    g_log_level = level;
}

void log_set_callback(LogCallback callback) {
    g_log_callback = callback;
}

void log_message(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < g_log_level) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    if (g_log_callback) {
        g_log_callback(level, file, line, buffer);
    } else {
        const char* level_str = "DEBUG";
        switch (level) {
            case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
            case LOG_LEVEL_INFO:  level_str = "INFO"; break;
            case LOG_LEVEL_WARN:  level_str = "WARN"; break;
            case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        }
        
        // Extract filename from path
        const char* filename = strrchr(file, '/');
        filename = filename ? filename + 1 : file;
        
        fprintf(stderr, "[%s] %s:%d: %s\n", level_str, filename, line, buffer);
    }
}

// ============================================================================
// Memory
// ============================================================================

void* mem_alloc(size_t size) {
    return malloc(size);
}

void* mem_alloc_aligned(size_t size, size_t alignment) {
    void* ptr = nullptr;
    #if defined(__ANDROID__)
        posix_memalign(&ptr, alignment, size);
    #else
        if (alignment <= 16) {
            ptr = aligned_alloc(alignment, size);
        } else {
            ptr = malloc(size);
        }
    #endif
    return ptr;
}

void mem_free(void* ptr) {
    if (ptr) free(ptr);
}

void* mem_copy(void* dest, const void* src, size_t size) {
    return memcpy(dest, src, size);
}

void* mem_set(void* dest, int value, size_t size) {
    return memset(dest, value, size);
}

// ============================================================================
// Time
// ============================================================================

uint64_t time_get_monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t time_get_real_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec;
}

void time_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}

void time_sleep_us(uint64_t us) {
    usleep(us);
}

// ============================================================================
// Thread
// ============================================================================

struct ThreadContext {
    ThreadFunc func;
    void* arg;
};

static void* thread_wrapper(void* arg) {
    auto* ctx = static_cast<ThreadContext*>(arg);
    ThreadFunc func = ctx->func;
    void* user_arg = ctx->arg;
    delete ctx;
    func(user_arg);
    return nullptr;
}

ThreadHandle thread_create(ThreadFunc func, void* arg) {
    auto* ctx = new ThreadContext;
    ctx->func = func;
    ctx->arg = arg;
    
    pthread_t thread;
    if (pthread_create(&thread, nullptr, thread_wrapper, ctx) != 0) {
        delete ctx;
        return nullptr;
    }
    
    return (ThreadHandle)thread;
}

void thread_destroy(ThreadHandle handle) {
    // Note: does not join, just releases handle
    (void)handle;
}

void thread_join(ThreadHandle handle) {
    pthread_join((pthread_t)handle, nullptr);
}

bool thread_is_current(ThreadHandle handle) {
    return pthread_equal((pthread_t)handle, pthread_self()) != 0;
}

// Mutex
struct MutexContext {
    pthread_mutex_t mutex;
    bool initialized;
};

MutexHandle mutex_create(void) {
    auto* ctx = new MutexContext;
    pthread_mutex_init(&ctx->mutex, nullptr);
    ctx->initialized = true;
    return ctx;
}

void mutex_destroy(MutexHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<MutexContext*>(handle);
    if (ctx->initialized) {
        pthread_mutex_destroy(&ctx->mutex);
    }
    delete ctx;
}

void mutex_lock(MutexHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<MutexContext*>(handle);
    pthread_mutex_lock(&ctx->mutex);
}

bool mutex_trylock(MutexHandle handle) {
    if (!handle) return false;
    auto* ctx = static_cast<MutexContext*>(handle);
    return pthread_mutex_trylock(&ctx->mutex) == 0;
}

void mutex_unlock(MutexHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<MutexContext*>(handle);
    pthread_mutex_unlock(&ctx->mutex);
}

// Semaphore
struct SemContext {
    sem_t sem;
    bool initialized;
};

SemHandle sem_create(int initial_count) {
    auto* ctx = new SemContext;
    if (sem_init(&ctx->sem, 0, initial_count) != 0) {
        delete ctx;
        return nullptr;
    }
    ctx->initialized = true;
    return ctx;
}

void sem_destroy(SemHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<SemContext*>(handle);
    if (ctx->initialized) {
        sem_destroy(&ctx->sem);
    }
    delete ctx;
}

void sem_wait(SemHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<SemContext*>(handle);
    sem_wait(&ctx->sem);
}

void sem_signal(SemHandle handle) {
    if (!handle) return;
    auto* ctx = static_cast<SemContext*>(handle);
    sem_post(&ctx->sem);
}

// ============================================================================
// File
// ============================================================================

int64_t file_get_size(const char* path) {
    if (!path) return -1;
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    
    return (int64_t)st.st_size;
}

bool file_exists(const char* path) {
    if (!path) return false;
    return access(path, F_OK) == 0;
}

int file_read(const char* path, void* buffer, size_t size) {
    if (!path || !buffer || size == 0) return -1;
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t bytes_read = read(fd, buffer, size);
    close(fd);
    
    return (int)bytes_read;
}

int file_write(const char* path, const void* buffer, size_t size) {
    if (!path || !buffer || size == 0) return -1;
    
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    
    ssize_t bytes_written = write(fd, buffer, size);
    close(fd);
    
    return (int)bytes_written;
}

// ============================================================================
// String
// ============================================================================

int str_copy(char* dest, size_t dest_size, const char* src) {
    if (!dest || !src) return -1;
    
    size_t src_len = strlen(src);
    size_t copy_len = src_len < dest_size - 1 ? src_len : dest_size - 1;
    
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    
    return (int)copy_len;
}

int str_concat(char* dest, size_t dest_size, ...) {
    if (!dest || dest_size == 0) return -1;
    
    dest[0] = '\0';
    
    va_list args;
    va_start(args, dest_size);
    
    const char* str;
    size_t total_len = 0;
    
    while ((str = va_arg(args, const char*)) != nullptr) {
        size_t len = strlen(str);
        if (total_len + len < dest_size) {
            strcat(dest, str);
            total_len += len;
        }
    }
    
    va_end(args);
    
    return (int)total_len;
}

bool str_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

char* str_trim(char* str) {
    if (!str) return nullptr;
    
    // Trim leading spaces
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    // Trim trailing spaces
    char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

// ============================================================================
// Math
// ============================================================================

float math_clamp(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

float math_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float math_deg_to_rad(float deg) {
    return deg * 0.01745329251994329576923690768489f;  // PI / 180
}

float math_rad_to_deg(float rad) {
    return rad * 57.295779513082320876798154814105f;  // 180 / PI
}

float math_round(float value) {
    return (float)((int)(value + 0.5f));
}

// ============================================================================
// Version
// ============================================================================

const char* sdk_version(void) {
    static char version[64];
    snprintf(version, sizeof(version), "%d.%d.%d",
             MEDICAL_DISPLAY_SDK_VERSION_MAJOR,
             MEDICAL_DISPLAY_SDK_VERSION_MINOR,
             MEDICAL_DISPLAY_SDK_VERSION_PATCH);
    return version;
}

#ifdef __cplusplus
}
#endif

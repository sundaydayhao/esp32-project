/**
 * @file reminder.c
 * @brief 提醒调度模块实现
 */

#include "reminder.h"
#include "storage.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "REMINDER";

/* 提醒数组 */
static reminder_t s_reminders[MAX_REMINDERS];
static uint8_t s_reminder_count = MAX_REMINDERS;

/* 当前活跃提醒索引（-1 表示无） */
static int s_active_idx = -1;

/* 默认提醒配置（早中晚各2次） */
static const reminder_config_t s_default_reminders[MAX_REMINDERS] = {
    {0,  8,  0, "\xe9\x99\x8d\xe5\x8e\x8b\xe8\x8d\xaf", "1\xe7\x89\x87", true},   // 降压药 1片 08:00
    {1,  8, 30, "\xe9\x99\x8d\xe7\xb3\x96\xe8\x8d\xaf", "1\xe7\x89\x87", true},   // 降糖药 1片 08:30
    {2, 12,  0, "\xe9\x99\x8d\xe5\x8e\x8b\xe8\x8d\xaf", "1\xe7\x89\x87", true},   // 降压药 1片 12:00
    {3, 18,  0, "\xe9\x99\x8d\xe5\x8e\x8b\xe8\x8d\xaf", "1\xe7\x89\x87", true},   // 降压药 1片 18:00
    {4, 21,  0, "\xe9\x99\x8d\xe7\xb3\x96\xe8\x8d\xaf", "1\xe7\x89\x87", false},  // 降糖药 1片 21:00 (禁用)
    {5, 22,  0, "\xe7\x9d\xa1\xe5\x89\x8d\xe8\x8d\xaf",  "1\xe7\x89\x87", false}, // 睡前药  1片 22:00 (禁用)
};

esp_err_t reminder_init(void)
{
    ESP_LOGI(TAG, "初始化提醒模块");

    /* 初始化运行时状态 */
    for (int i = 0; i < MAX_REMINDERS; i++) {
        memset(&s_reminders[i], 0, sizeof(reminder_t));
        s_reminders[i].state = REMINDER_STATE_IDLE;
    }

    /* 从 NVS 加载配置，失败则使用默认值 */
    esp_err_t ret = storage_load_reminders(s_reminders, MAX_REMINDERS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "从 NVS 加载提醒配置失败，使用默认配置");
        for (int i = 0; i < MAX_REMINDERS; i++) {
            memcpy(&s_reminders[i].config, &s_default_reminders[i],
                   sizeof(reminder_config_t));
        }
        /* 保存默认配置到 NVS */
        storage_save_reminders(s_reminders, MAX_REMINDERS);
    }

    ESP_LOGI(TAG, "提醒模块初始化完成，共 %d 个提醒", MAX_REMINDERS);
    for (int i = 0; i < MAX_REMINDERS; i++) {
        if (s_reminders[i].config.enabled) {
            ESP_LOGI(TAG, "  [%d] %02d:%02d %s %s",
                     i,
                     s_reminders[i].config.hour,
                     s_reminders[i].config.minute,
                     s_reminders[i].config.medication_name,
                     s_reminders[i].config.dosage);
        }
    }

    return ESP_OK;
}

void reminder_check(const struct tm *current_time)
{
    time_t now = mktime((struct tm *)current_time);

    for (int i = 0; i < MAX_REMINDERS; i++) {
        reminder_t *r = &s_reminders[i];

        if (!r->config.enabled) continue;
        if (r->state != REMINDER_STATE_IDLE) continue;

        /* 检查时间是否匹配（精确到分钟） */
        if (r->config.hour == current_time->tm_hour &&
            r->config.minute == current_time->tm_min) {

            /* 防止同一分钟内重复触发（检查上次触发时间） */
            if (r->last_checked > 0) {
                struct tm last_tm;
                localtime_r(&r->last_checked, &last_tm);
                if (last_tm.tm_hour == current_time->tm_hour &&
                    last_tm.tm_min == current_time->tm_min &&
                    last_tm.tm_mday == current_time->tm_mday) {
                    continue; /* 今天这个时间已经触发过了 */
                }
            }

            ESP_LOGI(TAG, "触发提醒 [%d]: %s %s",
                     i, r->config.medication_name, r->config.dosage);

            r->state = REMINDER_STATE_TRIGGERED;
            r->trigger_time = now;
            r->last_checked = now;
            s_active_idx = i;
            break; /* 一次只触发一个提醒 */
        }
    }
}

void reminder_check_timeout(void)
{
    if (s_active_idx < 0) return;

    reminder_t *r = &s_reminders[s_active_idx];
    time_t now = time(NULL);
    time_t elapsed = now - r->trigger_time;

    if (r->state == REMINDER_STATE_TRIGGERED) {
        if (elapsed >= REMINDER_TIMEOUT_SEC) {
            ESP_LOGW(TAG, "提醒 [%d] 超时，升级为持续蜂鸣", s_active_idx);
            r->state = REMINDER_STATE_ESCALATED;
        }
    } else if (r->state == REMINDER_STATE_ESCALATED) {
        if (elapsed >= REMINDER_TIMEOUT_SEC + REMINDER_ESCALATE_SEC) {
            ESP_LOGW(TAG, "提醒 [%d] 已错过", s_active_idx);
            r->state = REMINDER_STATE_MISSED;
            s_active_idx = -1;
        }
    }
}

bool reminder_confirm(void)
{
    if (s_active_idx < 0) return false;

    reminder_t *r = &s_reminders[s_active_idx];
    if (r->state == REMINDER_STATE_TRIGGERED ||
        r->state == REMINDER_STATE_ESCALATED) {

        ESP_LOGI(TAG, "提醒 [%d] 已确认：%s %s",
                 s_active_idx, r->config.medication_name, r->config.dosage);

        r->state = REMINDER_STATE_CONFIRMED;
        s_active_idx = -1;
        return true;
    }
    return false;
}

reminder_t *reminder_get_active(void)
{
    if (s_active_idx < 0) return NULL;
    reminder_t *r = &s_reminders[s_active_idx];
    if (r->state == REMINDER_STATE_TRIGGERED ||
        r->state == REMINDER_STATE_ESCALATED) {
        return r;
    }
    return NULL;
}

reminder_t *reminder_get_next(const struct tm *current_time)
{
    int current_minutes = current_time->tm_hour * 60 + current_time->tm_min;
    int min_diff = 24 * 60 + 1;
    reminder_t *next = NULL;

    for (int i = 0; i < MAX_REMINDERS; i++) {
        reminder_t *r = &s_reminders[i];
        if (!r->config.enabled) continue;
        if (r->state != REMINDER_STATE_IDLE) continue;

        int reminder_minutes = r->config.hour * 60 + r->config.minute;
        int diff = reminder_minutes - current_minutes;
        if (diff < 0) diff += 24 * 60; /* 跨天处理 */

        if (diff > 0 && diff < min_diff) {
            min_diff = diff;
            next = r;
        }
    }
    return next;
}

reminder_t *reminder_get_all(uint8_t *count)
{
    if (count) *count = s_reminder_count;
    return s_reminders;
}

esp_err_t reminder_set_config(uint8_t id, const reminder_config_t *config)
{
    if (id >= MAX_REMINDERS) return ESP_ERR_INVALID_ARG;

    memcpy(&s_reminders[id].config, config, sizeof(reminder_config_t));
    s_reminders[id].config.id = id;

    /* 如果当前提醒正在触发，重置状态 */
    if (s_active_idx == id) {
        s_reminders[id].state = REMINDER_STATE_IDLE;
        s_active_idx = -1;
    }

    /* 保存到 NVS */
    return storage_save_reminders(s_reminders, MAX_REMINDERS);
}

esp_err_t reminder_get_config(uint8_t id, reminder_config_t *config)
{
    if (id >= MAX_REMINDERS) return ESP_ERR_INVALID_ARG;
    memcpy(config, &s_reminders[id].config, sizeof(reminder_config_t));
    return ESP_OK;
}

const char *reminder_state_str(reminder_state_t state)
{
    switch (state) {
        case REMINDER_STATE_IDLE:       return "idle";
        case REMINDER_STATE_TRIGGERED:  return "triggered";
        case REMINDER_STATE_ESCALATED:  return "escalated";
        case REMINDER_STATE_CONFIRMED:  return "confirmed";
        case REMINDER_STATE_MISSED:     return "missed";
        default:                        return "unknown";
    }
}
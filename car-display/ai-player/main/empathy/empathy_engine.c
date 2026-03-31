/**
 * @file empathy_engine.c
 * @brief 共情引擎实现
 * 
 * 根据驾驶状态生成共情响应，提供驾驶陪伴功能
 * 支持多种情感状态和上下文感知的聊天回复
 */

#include "empathy_engine.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "empathy";

/* ============================================================
 * 共情消息数据库
 * ============================================================ */

/* 各情感状态的表情和名称 */
static const char *s_mood_emojis[MOOD_COUNT] = {
    [MOOD_HAPPY]    = "(^_^)",
    [MOOD_EXCITED]  = "(>_<)",
    [MOOD_WORRIED]  = "(>_<;)",
    [MOOD_SCARED]   = "(o_O)",
    [MOOD_SLEEPY]   = "(-_-)",
    [MOOD_CURIOUS]  = "(o_o)",
    [MOOD_RELAXED]  = "(^_^)",
    [MOOD_CHEERING] = "(^o^)",
};

static const char *s_mood_names[MOOD_COUNT] = {
    [MOOD_HAPPY]    = "开心",
    [MOOD_EXCITED]  = "兴奋",
    [MOOD_WORRIED]  = "担心",
    [MOOD_SCARED]   = "害怕",
    [MOOD_SLEEPY]   = "困倦",
    [MOOD_CURIOUS]  = "好奇",
    [MOOD_RELAXED]  = "放松",
    [MOOD_CHEERING] = "加油",
};

/* 各事件的共情消息（每个事件多条，随机选择） */
typedef struct {
    const char *messages[5];
    int count;
} event_messages_t;

static const event_messages_t s_event_messages[] = {
    [EVENT_NONE] = {
        .messages = {
            "一切正常，好好享受驾驶吧~",
            "路上注意安全哦！",
            "今天天气怎么样？",
            "有什么想聊的吗？",
            "我在这里陪着你~",
        },
        .count = 5,
    },
    [EVENT_SPEED_UP] = {
        .messages = {
            "哇，加速了！感觉风驰电掣~",
            "油门踩下去，感觉真爽！",
            "速度提升，注意前方路况哦！",
            "加速中，我抓紧了！",
            "嗖的一下，好刺激！",
        },
        .count = 5,
    },
    [EVENT_SPEED_DOWN] = {
        .messages = {
            "减速了，稳稳的~",
            "慢下来了，前面有情况吗？",
            "减速行驶，安全第一！",
            "踩刹车了，小心点~",
            "速度降下来了，放松一下~",
        },
        .count = 5,
    },
    [EVENT_HARD_BRAKE] = {
        .messages = {
            "哎呀！急刹车！没事吧？",
            "突然刹车，吓我一跳！前面没事吧？",
            "急刹！保持安全距离很重要哦！",
            "刹车！还好系了安全带~",
            "急停！深呼吸，没事的~",
        },
        .count = 5,
    },
    [EVENT_SHARP_TURN] = {
        .messages = {
            "急转弯！我差点飞出去了~",
            "转弯要慢一点哦，安全第一！",
            "哇，这个弯转得好帅！",
            "急转弯，注意离心力！",
            "弯道要减速哦，小心小心~",
        },
        .count = 5,
    },
    [EVENT_OVERSPEED] = {
        .messages = {
            "速度有点快了哦，注意限速！",
            "超速了！安全比时间更重要~",
            "这速度有点猛，慢一点吧！",
            "注意！速度超过限制了，减速哦！",
            "快慢慢来，安全到家才是最重要的！",
        },
        .count = 5,
    },
    [EVENT_TRAFFIC_JAM] = {
        .messages = {
            "堵车了，别着急，我陪你聊天~",
            "堵车是城市的日常，放松心情吧！",
            "堵车中...要不要听首歌？",
            "慢慢走，欣赏沿途风景~",
            "堵车别烦躁，深呼吸，我在这里~",
        },
        .count = 5,
    },
    [EVENT_SMOOTH_DRIVE] = {
        .messages = {
            "驾驶很平稳，棒棒的！",
            "这样开车真舒服，继续保持~",
            "平稳驾驶，是好司机的标志！",
            "一路顺畅，心情也好起来了~",
            "驾驶技术不错嘛！",
        },
        .count = 5,
    },
    [EVENT_TRIP_START] = {
        .messages = {
            "出发啦！今天去哪里？",
            "新的旅程开始了，一路平安！",
            "系好安全带，我们出发！",
            "旅途愉快！有我陪着你~",
            "开始行程，注意安全哦！",
        },
        .count = 5,
    },
    [EVENT_LONG_DRIVE] = {
        .messages = {
            "开了好久了，累了吗？要不要休息一下？",
            "长途驾驶注意疲劳，适时停车休息哦！",
            "已经开了很长时间了，喝点水吧~",
            "长途开车辛苦了！找个服务区歇歇脚？",
            "注意！长时间驾驶容易疲劳，要休息哦！",
        },
        .count = 5,
    },
    [EVENT_BUMPY_ROAD] = {
        .messages = {
            "路面有点颠，抓稳了！",
            "颠簸路段，减速慢行~",
            "哎哟，这路有点坑！",
            "颠簸中...我晕车了（开玩笑的）",
            "路不好走，小心底盘！",
        },
        .count = 5,
    },
};

/* ============================================================
 * 聊天回复数据库
 * ============================================================ */

typedef struct {
    const char *keywords[4];  // 关键词（任一匹配即触发）
    int keyword_count;
    const char *replies[4];   // 回复（随机选择）
    int reply_count;
} chat_rule_t;

static const chat_rule_t s_chat_rules[] = {
    {
        .keywords = {"你好", "hello", "hi", "嗨"},
        .keyword_count = 4,
        .replies = {
            "你好呀！我是你的驾驶小伙伴~",
            "嗨！今天开车去哪里呀？",
            "你好！路上注意安全哦~",
            "哈喽！有我陪着你，不寂寞~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"累", "疲劳", "困", "睡"},
        .keyword_count = 4,
        .replies = {
            "感觉累了吗？找个安全的地方停车休息一下吧！",
            "疲劳驾驶很危险哦，要不要找服务区休息？",
            "困了就停车睡一会儿，安全最重要！",
            "别硬撑！找个停车场小憩一下吧~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"堵车", "堵", "塞车", "拥堵"},
        .keyword_count = 4,
        .replies = {
            "堵车真烦人！不过有我陪你聊天，时间过得快一点~",
            "堵车就当是休息时间吧，放松放松~",
            "堵车中...要不要我给你讲个笑话？",
            "堵车别着急，急也没用，慢慢等吧~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"天气", "下雨", "下雪", "雾"},
        .keyword_count = 4,
        .replies = {
            "恶劣天气要减速慢行，保持安全距离！",
            "天气不好，开车要更加小心哦~",
            "雨天路滑，注意安全！",
            "能见度低的时候要开雾灯，慢慢开~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"音乐", "歌", "听歌", "播放"},
        .keyword_count = 4,
        .replies = {
            "开车听音乐心情好！不过音量别太大哦~",
            "好主意！音乐让驾驶更愉快~",
            "来首轻快的歌，让旅途更美好！",
            "音乐相伴，一路欢歌~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"饿", "吃饭", "吃东西", "饥饿"},
        .keyword_count = 4,
        .replies = {
            "饿了就找个地方停车吃饭吧，别饿着开车！",
            "肚子饿了？找个服务区补充能量~",
            "开车不能吃东西哦，找个停车场再吃！",
            "饿着肚子开车注意力不集中，快去吃饭！",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"快", "速度", "加速", "飙车"},
        .keyword_count = 4,
        .replies = {
            "速度要适中哦，安全第一！",
            "想快也要在安全范围内~",
            "飙车很危险！慢慢来，安全到家才是目标~",
            "速度感很爽，但安全更重要！",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"导航", "路", "迷路", "方向"},
        .keyword_count = 4,
        .replies = {
            "迷路了？打开导航，我们一起找路~",
            "不认识路没关系，导航来帮忙！",
            "找不到路？停车看看地图吧~",
            "导航是好帮手，跟着走就对了！",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"停车", "停", "泊车", "车位"},
        .keyword_count = 4,
        .replies = {
            "找停车位有点难？慢慢找，别着急~",
            "停车要注意周围环境，小心刮蹭！",
            "倒车入库要慢，看好后视镜~",
            "停车场找位置要有耐心~",
        },
        .reply_count = 4,
    },
    {
        .keywords = {"谢谢", "感谢", "棒", "厉害"},
        .keyword_count = 4,
        .replies = {
            "不客气！能帮到你我很开心~",
            "嘿嘿，我会继续努力的！",
            "谢谢夸奖！一起安全驾驶~",
            "能陪伴你是我的荣幸！",
        },
        .reply_count = 4,
    },
};

#define CHAT_RULES_COUNT (sizeof(s_chat_rules) / sizeof(s_chat_rules[0]))

/* ============================================================
 * 内部状态
 * ============================================================ */

static empathy_mood_t s_current_mood = MOOD_HAPPY;
static empathy_response_t s_current_response = {0};
static SemaphoreHandle_t s_mutex = NULL;
static empathy_callback_t s_callback = NULL;

/* 驾驶状态跟踪 */
static float s_last_speed = 0;
static int64_t s_last_event_time = 0;
static int64_t s_trip_start_time = 0;
static uint32_t s_low_speed_duration_ms = 0;
static uint32_t s_smooth_drive_duration_ms = 0;
static bool s_trip_started = false;

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

static void trigger_response(empathy_mood_t mood, empathy_event_t event,
                              uint32_t duration_ms)
{
    int64_t now = esp_timer_get_time() / 1000;

    /* 防止频繁触发（同一事件至少间隔3秒） */
    if (event != EVENT_NONE && (now - s_last_event_time) < 3000) {
        return;
    }

    const event_messages_t *msgs = &s_event_messages[event];
    int idx = rand() % msgs->count;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_current_mood = mood;
    s_current_response.mood = mood;
    s_current_response.event = event;
    s_current_response.message = msgs->messages[idx];
    s_current_response.emoji = s_mood_emojis[mood];
    s_current_response.display_duration_ms = duration_ms;
    xSemaphoreGive(s_mutex);

    s_last_event_time = now;

    ESP_LOGI(TAG, "[%s] %s: %s",
             s_mood_names[mood],
             s_mood_emojis[mood],
             s_current_response.message);

    if (s_callback) {
        s_callback(&s_current_response);
    }
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t empathy_engine_init(void)
{
    ESP_LOGI(TAG, "初始化共情引擎");

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_current_mood = MOOD_HAPPY;
    s_trip_start_time = esp_timer_get_time() / 1000;

    /* 初始欢迎消息 */
    s_current_response.mood = MOOD_HAPPY;
    s_current_response.event = EVENT_TRIP_START;
    s_current_response.message = s_event_messages[EVENT_TRIP_START].messages[0];
    s_current_response.emoji = s_mood_emojis[MOOD_HAPPY];
    s_current_response.display_duration_ms = 5000;

    ESP_LOGI(TAG, "共情引擎初始化完成");
    return ESP_OK;
}

void empathy_engine_register_callback(empathy_callback_t cb)
{
    s_callback = cb;
}

void empathy_engine_update(const speed_data_t *speed,
                            const driving_behavior_t *behavior)
{
    if (speed == NULL) return;

    float current_speed = speed->speed_kmh;
    float accel = speed->acceleration;
    int64_t now_ms = esp_timer_get_time() / 1000;

    /* 行程开始检测 */
    if (!s_trip_started && current_speed > 5.0f) {
        s_trip_started = true;
        trigger_response(MOOD_HAPPY, EVENT_TRIP_START, 5000);
        return;
    }

    /* 超速检测 */
    if (current_speed > SPEED_VERY_FAST) {
        trigger_response(MOOD_WORRIED, EVENT_OVERSPEED, 4000);
        return;
    }

    /* 急刹车检测 */
    if (behavior != NULL && behavior->is_braking_hard) {
        trigger_response(MOOD_SCARED, EVENT_HARD_BRAKE, 3000);
        return;
    }

    /* 急转弯检测 */
    if (behavior != NULL &&
        fabsf(behavior->steering_angle) > TURN_SHARP) {
        trigger_response(MOOD_CURIOUS, EVENT_SHARP_TURN, 3000);
        return;
    }

    /* 颠簸路面检测 */
    if (behavior != NULL && behavior->is_bumpy) {
        trigger_response(MOOD_CURIOUS, EVENT_BUMPY_ROAD, 2000);
        return;
    }

    /* 急加速检测 */
    if (accel > ACCEL_HARD) {
        trigger_response(MOOD_EXCITED, EVENT_SPEED_UP, 3000);
        return;
    }

    /* 急减速检测（非急刹） */
    if (accel < DECEL_MODERATE && accel > DECEL_HARD) {
        trigger_response(MOOD_WORRIED, EVENT_SPEED_DOWN, 2000);
        return;
    }

    /* 堵车/低速检测 */
    if (current_speed < SPEED_SLOW && current_speed > 0) {
        s_low_speed_duration_ms += EMPATHY_CHECK_INTERVAL_MS;
        if (s_low_speed_duration_ms > 30000) {  // 低速超过30秒
            trigger_response(MOOD_CHEERING, EVENT_TRAFFIC_JAM, 4000);
            s_low_speed_duration_ms = 0;
        }
    } else {
        s_low_speed_duration_ms = 0;
    }

    /* 长途驾驶检测 */
    uint32_t trip_duration_min = (uint32_t)((now_ms - s_trip_start_time) / 60000);
    if (trip_duration_min > 0 && trip_duration_min % 60 == 0) {
        trigger_response(MOOD_WORRIED, EVENT_LONG_DRIVE, 5000);
        return;
    }

    /* 平稳驾驶检测 */
    if (current_speed > SPEED_SLOW && current_speed < SPEED_FAST &&
        fabsf(accel) < ACCEL_GENTLE &&
        (behavior == NULL || !behavior->is_turning_left && !behavior->is_turning_right)) {
        s_smooth_drive_duration_ms += EMPATHY_CHECK_INTERVAL_MS;
        if (s_smooth_drive_duration_ms > 60000) {  // 平稳驾驶超过60秒
            trigger_response(MOOD_RELAXED, EVENT_SMOOTH_DRIVE, 3000);
            s_smooth_drive_duration_ms = 0;
        }
    } else {
        s_smooth_drive_duration_ms = 0;
    }

    /* 高速行驶状态 */
    if (current_speed >= SPEED_FAST && current_speed < SPEED_VERY_FAST) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_mood = MOOD_EXCITED;
        xSemaphoreGive(s_mutex);
    } else if (current_speed >= SPEED_NORMAL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_mood = MOOD_HAPPY;
        xSemaphoreGive(s_mutex);
    } else if (current_speed < SPEED_SLOW && current_speed > 0) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_mood = MOOD_CHEERING;
        xSemaphoreGive(s_mutex);
    } else if (current_speed == 0) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_mood = MOOD_RELAXED;
        xSemaphoreGive(s_mutex);
    }

    s_last_speed = current_speed;
}

empathy_mood_t empathy_engine_get_mood(void)
{
    return s_current_mood;
}

void empathy_engine_get_response(empathy_response_t *response)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *response = s_current_response;
    xSemaphoreGive(s_mutex);
}

void empathy_engine_chat(const char *input, char *output, size_t output_len)
{
    if (input == NULL || output == NULL || output_len == 0) return;

    /* 遍历聊天规则，查找匹配关键词 */
    for (int i = 0; i < (int)CHAT_RULES_COUNT; i++) {
        const chat_rule_t *rule = &s_chat_rules[i];
        for (int j = 0; j < rule->keyword_count; j++) {
            if (strstr(input, rule->keywords[j]) != NULL) {
                /* 找到匹配，随机选择回复 */
                int idx = rand() % rule->reply_count;
                snprintf(output, output_len, "%s", rule->replies[idx]);
                ESP_LOGI(TAG, "聊天匹配关键词[%s] -> %s", rule->keywords[j], output);
                return;
            }
        }
    }

    /* 没有匹配规则，根据当前驾驶状态生成通用回复 */
    empathy_mood_t mood = empathy_engine_get_mood();
    const char *generic_replies[] = {
        "嗯嗯，我听着呢~",
        "是这样啊，继续说~",
        "哦？有意思！",
        "我明白你的意思~",
        "好的，我记住了！",
    };

    /* 根据情感状态添加驾驶相关提示 */
    int idx = rand() % 5;
    if (mood == MOOD_WORRIED || mood == MOOD_SCARED) {
        snprintf(output, output_len, "%s 不过现在先专注开车哦！",
                 generic_replies[idx]);
    } else if (mood == MOOD_CHEERING) {
        snprintf(output, output_len, "%s 堵车中，慢慢聊~",
                 generic_replies[idx]);
    } else {
        snprintf(output, output_len, "%s", generic_replies[idx]);
    }
}

const char *empathy_mood_to_emoji(empathy_mood_t mood)
{
    if (mood >= MOOD_COUNT) return "(^_^)";
    return s_mood_emojis[mood];
}

const char *empathy_mood_to_name(empathy_mood_t mood)
{
    if (mood >= MOOD_COUNT) return "未知";
    return s_mood_names[mood];
}
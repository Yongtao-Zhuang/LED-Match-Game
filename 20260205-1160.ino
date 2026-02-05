#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

// ============================================================
// =====                0. 核心硬件与内存安全                =====
// ============================================================
// [RETAINED - 完全保留自V25]
#define LED_PIN     18          // WS2812B灯带数据信号引脚
#define MAX_LEDS    1000        // 物理灯珠缓冲区上限
CRGB leds[MAX_LEDS];            // FastLED主显存数组
Preferences preferences;        // 非易失性存储，用于保存38个参数
WebServer server(80);           // 网页后台服务器对象

// ============================================================
// =====                1. 变量定义区 (全量 38 个参数)       =====
// ============================================================
// [RETAINED - 严格遵循历史变量定义，严禁修改任何旧变量名]

// --- 核心硬件与系统参数 ---
int NUM_LEDS = 120;              // 灯珠总数，默认120颗
int DEVICE_ROLE = 2;             // 设备角色：0-强制主控, 1-强制从机, 2-自动竞选
int resolvedRole = 0;            // 程序启动后最终确定的运行身份
int INITIAL_BRIGHTNESS = 40;     // 用户设定的基准亮度等级 (0-255)
int MAX_LEVEL_LIMIT = 10;        // 关卡上限，最多10关
int GAME_MODE = 0;               // 游戏模式：0-普通, 1-节奏, 2-混合
int GRAVITY_SETTING = 2;         // 重力模式：0-正向, 1-反向, 2-自动切换

// --- 游戏平衡核心参数 ---
int currentLevel = 1;            // 当前游戏关卡，从1开始
int INITIAL_TIGER_POS = 110;     // 老虎初始位置（逻辑坐标）
int ELEPHANT_POS   = 1;          // 大象位置（逻辑坐标）
int INITIAL_SPEED     = 5;       // 子弹初始速度
int MAX_SPEED         = 100;     // 子弹最大速度
int WIN_HIT_REQUIRED  = 4;       // 胜利所需击中次数
int COMBO_TARGET      = 8;       // 玩家连击达到此数触发老虎长休
int JUDGE_ZONE_WIDTH = 4;        // 判定区宽度（节奏模式）
int JUDGE_ZONE_START = 4;        // 判定区起始位置（节奏模式）
int JUDGE_ZONE_BRIGHTNESS = 120; // 判定区亮度 (0-255)
int JUDGE_ZONE_COLOR_R = 120;    // 判定区颜色 - 红色分量 (0-255)
int JUDGE_ZONE_COLOR_G = 120;    // 判定区颜色 - 绿色分量 (0-255)
int JUDGE_ZONE_COLOR_B = 120;    // 判定区颜色 - 蓝色分量 (0-255)
int BASE_RHYTHM_TARGET = 5;      // 节奏模式基础目标击中数
int HIT_INCREASE_PER_LEVEL = 2;  // [NEW] 每关增加的胜利所需击中数


// --- 动作步长与惩罚变量 ---
int TIGER_MOVE_FORWARD  = 1;     // 老虎前进步长
int TIGER_MOVE_BACKWARD = 3;     // 老虎后退步长
int PROJECTILE_TO_FORWARD = 1;   // 子弹击中大象后老虎前进所需数量
int SHIELD_TO_BACKWARD     = 1;  // 盾牌击中老虎后老虎后退所需数量
int COMBO_TO_DOUBLE        = 5;  // 连续漏球惩罚翻倍阈值

// --- 手感精调参数 ---
int MAX_BULLETS_ON_SCREEN = 6;      // 同屏最大子弹数
int SPEED_GROWTH_PER_LEVEL = 4;     // 每关速度增长值
int GAP_MIN_MS = 1200;              // 最小发射间隔(毫秒)
int GAP_MAX_MS = 2500;              // 最大发射间隔(毫秒)
int GAP_REDUCTION_PER_LEVEL = 200;  // 每关间隔减少值(毫秒)
float PHYSICS_DIVISOR = 80.0;       // 逻辑时钟下的位移分母，影响子弹移动速度

// [NEW - 波次扫射控制参数]
int BURST_PROB_INC = 5;             // 每关增加的扫射百分比概率 (5%)
int BURST_MAX_COUNT = 5;            // 扫射波次内最大子弹连发数

// --- 时间、音效与尺寸 ---
int REST_TIME_NORMAL     = 3000; // 老虎普通休息时间(毫秒)
int REST_TIME_LONG       = 10;   // 连击奖励：老虎长休时间(秒)
int EFFECT_DURATION      = 5000; // 胜负特效持续时间(毫秒)
int TIGER_BREATH_SPEED = 10;     // 老虎呼吸动画速度
int EFFECT_BREATH_SPEED = 15;    // 特效呼吸动画速度
int BULLET_SIZE = 1;            // 子弹大小（占用灯珠数）
int SHIELD_SIZE = 1;            // 盾牌大小（占用灯珠数）
int TIGER_SIZE  = 2;            // 老虎大小（占用灯珠数）

// [NEW - 波次攻击运行时计数器]
int burstProjectilesLeft = 0;   // 当前扫射波次中剩余待发射的子弹数
int burstStackCount = 0;        // 当前扫射波次中已经生成的叠色子弹数

// --- 运行时内部状态控制变量 ---
float tigerPos = 57;            // 老虎当前位置（逻辑坐标）
unsigned long lastTigerAction = 0;    // 上次老虎动作时间戳
unsigned long lastUpdate = 0;          // 上次游戏更新时间戳
bool isResting = false;                // 老虎是否处于休息状态
int waveCount = 0;                      // 当前波次剩余子弹数
int comboCounter = 0;        // 玩家当前连击数
bool longRestPending = false;   // 长休状态机挂起标识
bool isReturning = false;    // 老虎是否正在归位
float tigerReturnPos = 0;    // 老虎归位目标位置
int homeHitCount = 0;        // 老虎在起始位置被击中的次数
int forwardCounter = 0;      // 子弹击中大象计数器
int backwardCounter = 0;      // 盾牌击中老虎计数器
int shieldBrokenCombo = 0;    // 连续打破盾牌计数器
bool showWinEffect = false;    // 是否显示胜利特效
bool showLoseEffect = false;  // 是否显示失败特效
unsigned long effectStartTime = 0; // 特效开始时间戳
bool isGravityInverted = false; // 重力是否反转
bool tigerLeftHome = false;  // 老虎是否曾经离开过起始位置
char dynamic_ssid[64]; // 动态生成的WiFi SSID
String eventQueue = ""; // 事件队列，用于与网页通信



// [NEW - 亮度突破功能专用：每一帧统计每个位置的占用情况]
uint8_t pixelOccupancy[MAX_LEDS]; 
uint8_t pixelCurrentIndex[MAX_LEDS]; // [NEW - 用于渲染时的轮播计数] 

struct Projectile { float pos; float speed; CRGB color; bool active; };
Projectile projectiles[25];

// [RETAINED - 50 颗盾牌容器支持]
struct Shield { float pos; CRGB color; bool active; };
Shield shields[50]; 

struct struct_message { char game_id[5]; char command; };
struct_message myData;
const char* GAME_ID = "LED1";
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================
// =====                2. 核心逻辑工具函数                  =====
// ============================================================

// [RETAINED - 坐标转换]
/**
 * 将逻辑坐标转换为物理坐标
 * 考虑重力反转的影响
 * @param logicalPos 逻辑坐标
 * @return 转换后的物理坐标
 */
int getPhysPos(float logicalPos) {
    int pos = (int)logicalPos;
    if (isGravityInverted) {
        // 重力反转时，坐标需要翻转
        return (NUM_LEDS - 1 - pos);
    }
    return pos;
}

// [RETAINED - 消息同步]
/**
 * 添加特效事件到事件队列
 * 用于与网页端通信，触发音效和动画
 * @param type 事件类型字符串
 */
void addEffect(String type) {
    if (eventQueue != "") {
        eventQueue += ",";
    }
    eventQueue += type;
}

// [RETAINED - 模式分流判断]
/**
 * 获取当前游戏子模式
 * 根据游戏模式和当前关卡确定实际运行的子模式
 * @return 子模式：0-普通模式, 1-节奏模式
 */
int getSubMode() {
    if (GAME_MODE == 0) return 0;      // 普通模式
    if (GAME_MODE == 1) return 1;      // 节奏模式
    if (((currentLevel - 1) / 2) % 2 == 0) {
        return 0;  // 混合模式：偶数关卡对普通模式
    }
    return 1;      // 混合模式：奇数关卡对节奏模式
}

// [NEW - 亮度突破核心：软件亮度缩放渲染]
/**
 * 应用软件亮度缩放
 * 手动计算每个颜色通道的亮度，实现亮度突破效果
 * @param color 原始颜色
 * @param brightness 亮度值 (0-255)
 * @return 亮度缩放后的颜色
 */
CRGB applySoftwareBrightness(CRGB color, int brightness) {
    // brightness 0-255，手动计算每一个通道，实现亮度爆发
    uint8_t out_r = (uint32_t)(color.r * brightness) / 255;
    uint8_t out_g = (uint32_t)(color.g * brightness) / 255;
    uint8_t out_b = (uint32_t)(color.b * brightness) / 255;
    return CRGB(out_r, out_g, out_b);
}

// [MODIFIED - 游戏重置逻辑：增加了新的状态重置]
/**
 * 重置游戏状态
 * 初始化所有游戏变量，准备开始新的游戏或关卡
 */
void resetGame() {
    showWinEffect = false; 
    showLoseEffect = false;
    
    // 硬件总闸已在 setup 锁定为 255，不再在此修改
    INITIAL_TIGER_POS = NUM_LEDS - TIGER_SIZE - 1; 
    if (INITIAL_TIGER_POS < 10) {
        INITIAL_TIGER_POS = 10; 
    }
    tigerPos = (float)INITIAL_TIGER_POS;
    homeHitCount = 0; 
    comboCounter = 0; 
    forwardCounter = 0; 
    backwardCounter = 0; 
    shieldBrokenCombo = 0;
    burstProjectilesLeft = 0; // 重置扫射状态
    burstStackCount = 0;      // 重置叠色计数器
    
    isReturning = false; 
    longRestPending = false; 
    isResting = false;
    tigerLeftHome = false;
    
    // 重置所有子弹
    for(int i = 0; i < 25; i++) {
        projectiles[i].active = false;
    }
    // 重置所有盾牌
    for(int i = 0; i < 50; i++) {
        shields[i].active = false; 
    }
    
    // 设置重力模式
    if (GRAVITY_SETTING == 0) {
        isGravityInverted = false;
    } else if (GRAVITY_SETTING == 1) {
        isGravityInverted = true;
    } else {
        // 自动切换模式：偶数关卡反转重力
        isGravityInverted = (currentLevel % 2 == 0);
    }
    
    // 计算当前关卡的波次子弹数
    float waveFactor = pow(1.2, currentLevel - 1); 
    waveCount = constrain(random(2, 5) * waveFactor, 1, 40); 
    lastTigerAction = millis();
}

// ============================================================
// =====                3. 游戏引擎逻辑核心                  =====
// ============================================================

// [NEW - 叠色限制核心检查函数]
/**
 * 检查目标位置是否已达到叠色上限
 * 用于在发射和移动前检测是否超过 2 颗子弹重叠
 * @param targetLogicalPos 目标逻辑位置
 * @return true 表示已经叠了 2 颗或以上，不能再叠了
 */
bool isStackFullAt(float targetLogicalPos) {
    int checkCount = 0;
    for (int i = 0; i < 25; i++) {
        if (projectiles[i].active) {
            // 只要物理距离在 0.5 格以内，就认为是完全重叠
            if (abs(projectiles[i].pos - targetLogicalPos) < 0.5) {
                checkCount++;
            }
        }
    }
    // 返回 true 表示已经叠了 2 颗或以上，不能再叠了
    return (checkCount >= 2);
}

// [MODIFIED - 子弹发射逻辑：嵌入叠色上限检查与扫射支持]
/**
 * 生成新的子弹
 * 包含叠色上限检查和扫射模式支持
 */
void spawnProjectile() {
    // 1. 枪口排他检查：如果枪口位置 (tigerPos - 1.0) 已经叠了2颗，暂时哑火
    if (isStackFullAt(tigerPos - 1.0)) {
        return; 
    }

    for (int i = 0; i < 25; i++) {
        if (!projectiles[i].active) {
            projectiles[i].active = true;
            projectiles[i].pos = (float)tigerPos - 1.0;
            projectiles[i].speed = (float)INITIAL_SPEED + (float)((currentLevel - 1) * SPEED_GROWTH_PER_LEVEL);
            
            int colorRoll = random(0, 3);
            if (colorRoll == 0) projectiles[i].color = CRGB::Red;
            else if (colorRoll == 1) projectiles[i].color = CRGB::Green;
            else projectiles[i].color = CRGB::Blue;

            // [watermelon - 完全保留 V25 叠色球概率生成逻辑]
            // 注意：只有当刚才发射位置只有 1 颗子弹时，才允许生成主动叠色球
            // [MODIFIED - 扫射模式下限制叠色数量，最多2个]
            if (currentLevel >= 2 && random(0, 100) < (currentLevel * 10)) {
                // 检查是否在扫射模式下，并且叠色计数未达到上限
                bool canSpawnStack = true;
                if (burstProjectilesLeft > 0 && burstStackCount >= 2) {
                    canSpawnStack = false;
                }
                
                // 再次检查确认当前位置只有刚才发出的那 1 颗，满足“双叠”上限
                if (canSpawnStack && !isStackFullAt(projectiles[i].pos)) {
                    for (int j = 0; j < 25; j++) {
                        if (!projectiles[j].active && j != i) {
                            projectiles[j] = projectiles[i]; 
                            int c2; 
                            do { c2 = random(0, 3); } while(c2 == colorRoll);
                            if (c2 == 0) projectiles[j].color = CRGB::Red;
                            else if (c2 == 1) projectiles[j].color = CRGB::Green;
                            else projectiles[j].color = CRGB::Blue;
                            
                            // 扫射模式下，增加叠色计数
                            if (burstProjectilesLeft > 0) {
                                burstStackCount++;
                            }
                            break;
                        }
                    }
                }
            }
            break;
        }
    }
}

// [MODIFIED - 老虎决策逻辑：全量重型嵌入波次攻击扫射机制]
/**
 * 处理老虎的决策逻辑
 * 包括休息判断、子弹发射和扫射模式切换
 */
void handleTigerLogic() {
    unsigned long now_tick = millis();
    
    // [RETAINED - 严格保护的老虎长休/普通休息判断]
    if (isResting) {
        unsigned long current_dur = 0;
        if (longRestPending) {
            current_dur = (unsigned long)REST_TIME_LONG * 1000;
        } else {
            current_dur = (unsigned long)REST_TIME_NORMAL;
        }
        if (now_tick - lastTigerAction >= current_dur) {
            isResting = false; 
            longRestPending = false; 
            float waveFactor = pow(1.2, currentLevel - 1);
            waveCount = constrain(random(2, 5) * waveFactor, 1, 40);
            lastTigerAction = now_tick;
        }
        return;
    }

    if (waveCount > 0) {
        static unsigned long lastSpawnTime = 0;
        static unsigned long nextGap = 1000; 
        
        int currentActiveCount = 0;
        for (int i = 0; i < 25; i++) {
            if (projectiles[i].active) currentActiveCount++;
        }
        
        int dynamicMaxBullets = MAX_BULLETS_ON_SCREEN + (currentLevel - 1);
        
        if (currentActiveCount < dynamicMaxBullets) { 
            if (now_tick - lastSpawnTime > nextGap) {
                // 执行子弹发射
                spawnProjectile(); 
                waveCount--; 
                lastSpawnTime = now_tick;

                // [NEW - 扫射状态机切换逻辑]
                if (burstProjectilesLeft > 0) {
                    // 当前正处于波次扫射序列中
                    burstProjectilesLeft--;
                    // 波次内采用固定极速间隔 (150-280ms)
                    nextGap = random(150, 280); 
                    
                    // 当扫射结束时，重置叠色计数器
                    if (burstProjectilesLeft == 0) {
                        burstStackCount = 0;
                    }
                } 
                else {
                    // 非扫射波次，根据关卡概率判定是否开启新的扫射
                    int burstRollValue = random(0, 100);
                    int burstThreshold = currentLevel * BURST_PROB_INC;
                    
                    if (burstRollValue < burstThreshold) {
                        // 触发！进入波次攻击扫射模式
                        int currentBurstCap = 1 + (currentLevel / 2);
                        if (currentBurstCap > BURST_MAX_COUNT) {
                            currentBurstCap = BURST_MAX_COUNT;
                        }
                        // 设置剩余扫射数 (2颗到上限随机)
                        burstProjectilesLeft = random(2, currentBurstCap + 1);
                        nextGap = random(150, 280); 
                    } 
                    else {
                        // 常规逻辑：保留 45/55 的灵魂随机节奏分流
                        int cMin = max(200, GAP_MIN_MS - (currentLevel - 1) * GAP_REDUCTION_PER_LEVEL);
                        int cMax = max(400, GAP_MAX_MS - (currentLevel - 1) * GAP_REDUCTION_PER_LEVEL);
                        
                        if (random(0, 100) < 45) {
                            nextGap = random(cMin, cMin + 200); 
                        } else {
                            nextGap = random(cMax / 2, cMax);    
                        }
                    }
                }

                if (waveCount == 0) { 
                    isResting = true; 
                    lastTigerAction = now_tick; 
                }
            }
        }
    }
}

// [RETAINED - 物理引擎核心，严禁为了美观精简]
/**
 * 更新游戏物理状态
 * 处理子弹移动、碰撞检测、老虎移动等
 */
void updatePhysics() {
    if (isReturning || showWinEffect || showLoseEffect) return;
    int cur_sub_mode = getSubMode();
    // [MODIFIED] 使用动态变量 HIT_INCREASE_PER_LEVEL 代替硬编码
    int dyn_win_hits = WIN_HIT_REQUIRED + (currentLevel - 1) * HIT_INCREASE_PER_LEVEL;
    int dyn_max_speed = MAX_SPEED + (currentLevel - 1) * 10;
    
    // 检测老虎是否离开起始位置
    if (!tigerLeftHome && tigerPos < INITIAL_TIGER_POS - 1) {
        tigerLeftHome = true;
    }

    if (cur_sub_mode == 0) {
        // [RETAINED - 处理 50 颗盾牌位移]
        for (int i = 0; i < 50; i++) {
            if (shields[i].active) {
                shields[i].pos += 0.5;
                if (shields[i].pos >= tigerPos) {
                    shields[i].active = false; 
                    addEffect("hit_tiger"); 
                    if (tigerPos < INITIAL_TIGER_POS) {
                        if (++backwardCounter >= SHIELD_TO_BACKWARD) {
                            tigerPos += TIGER_MOVE_BACKWARD;
                            if (tigerPos > INITIAL_TIGER_POS) tigerPos = INITIAL_TIGER_POS;
                            backwardCounter = 0;
                        }
                    } else if (++homeHitCount >= dyn_win_hits) { 
                        isReturning = true; tigerReturnPos = tigerPos; addEffect("win"); 
                    }
                }
            }
        }
    }

    // 处理子弹物理
    for (int i = 0; i < 25; i++) {
        if (projectiles[i].active) {
            // [NEW - 物理层追尾叠色限制核心]
            float futurePos = projectiles[i].pos - (projectiles[i].speed / PHYSICS_DIVISOR);
            
            // 预判扫描：如果移动到 futurePos 会导致 3 颗或以上子弹重合，则该子弹强制“刹车”或降速
            bool wouldOverStack = false;
            int potentialStack = 0;
            for (int m = 0; m < 25; m++) {
                if (m != i && projectiles[m].active) {
                    if (abs(projectiles[m].pos - futurePos) < 0.5) {
                        potentialStack++;
                    }
                }
            }
            if (potentialStack >= 2) wouldOverStack = true;

            if (!wouldOverStack) {
                projectiles[i].pos = futurePos; // 允许正常移动
            } else {
                // 如果会造成 3 叠，子弹暂时以极低速蠕动，直到前方的球移开
                projectiles[i].pos -= 0.01; 
            }

            if (cur_sub_mode == 0) {
                for (int j = 0; j < 50; j++) {
                    float collider = 1.2 + (BULLET_SIZE * 0.1);
                    if (shields[j].active && abs(projectiles[i].pos - shields[j].pos) < collider) {
                        if (projectiles[i].color == shields[j].color) {
                            projectiles[i].active = false; 
                            shields[j].active = false;
                            comboCounter++; 
                            shieldBrokenCombo = 0; 
                            addEffect("match");
                            if (comboCounter >= COMBO_TARGET) { longRestPending = true; }
                        } else {
                            shields[j].active = false; 
                            addEffect("break");
                            projectiles[i].speed = min((float)projectiles[i].speed + 15.0f, (float)dyn_max_speed);
                            comboCounter = 0; 
                            shieldBrokenCombo++; // [watermelon - 惩罚累计]
                        }
                    }
                }
            }
            
            if (projectiles[i].active && projectiles[i].pos <= ELEPHANT_POS) {
                projectiles[i].active = false; 
                addEffect("hit_ele"); 
                comboCounter = 0;
                if (++forwardCounter >= PROJECTILE_TO_FORWARD) {
                    // [watermelon - 惩罚翻倍逻辑执行]
                    int tigerStep = (shieldBrokenCombo >= COMBO_TO_DOUBLE) ? TIGER_MOVE_FORWARD * 2 : TIGER_MOVE_FORWARD;
                    tigerPos -= tigerStep; 
                    forwardCounter = 0; 
                    shieldBrokenCombo = 0;
                    if (tigerPos <= ELEPHANT_POS + 1) { 
                        showLoseEffect = true; 
                        effectStartTime = millis(); 
                        addEffect("lose"); 
                    }
                }
            }
        }
    }
}

// ============================================================
// =====                4. 渲染引擎 (重型亮度突破版)           =====
// ============================================================

// [RETAINED - 严格执行亮度突破重绘逻辑]
/**
 * 绘制游戏画面
 * 包括大象、判定区、子弹、盾牌和老虎的绘制
 * 实现了亮度突破和颜色叠加效果
 */
void drawGame() {
    FastLED.clear();
    int current_play_mode = getSubMode();
    
    // [NEW] 1. 占用扫描核心：记录每一个物理灯珠被覆盖的子弹数量
    for(int i = 0; i < NUM_LEDS; i++) {
        pixelOccupancy[i] = 0;
        pixelCurrentIndex[i] = 0;
    }
    for (int i = 0; i < 25; i++) {
        if (projectiles[i].active) {
            for (int b = 0; b < BULLET_SIZE; b++) {
                int physical_idx = getPhysPos(projectiles[i].pos + b);
                if (physical_idx >= 0 && physical_idx < NUM_LEDS) {
                    pixelOccupancy[physical_idx]++;
                }
            }
        }
    }

    // 2. 绘制大象 (使用软件缩放亮度)
    int elephant_physical_p = getPhysPos(ELEPHANT_POS);
    if (elephant_physical_p >= 0 && elephant_physical_p < NUM_LEDS) {
        CRGB elephant_original = CHSV(42, 255, beatsin8(20, 160, 255));
        leds[elephant_physical_p] = applySoftwareBrightness(elephant_original, INITIAL_BRIGHTNESS);
    }
    
    // 3. 绘制判定区 (仅针对节奏模式)
    if (current_play_mode == 1) { 
        for(int i = 0; i < JUDGE_ZONE_WIDTH; i++) {
            int pp_idx = getPhysPos(JUDGE_ZONE_START + i);
            if(pp_idx >= 0 && pp_idx < NUM_LEDS) {
                leds[pp_idx] = applySoftwareBrightness(CRGB(JUDGE_ZONE_COLOR_R, JUDGE_ZONE_COLOR_G, JUDGE_ZONE_COLOR_B), INITIAL_BRIGHTNESS);
            }
        }
    }

    // 4. 绘制子弹 (轮播闪烁版：重叠时交替显示高亮单色)
    for (int i = 0; i < 25; i++) {
        if (projectiles[i].active) {
            for (int b = 0; b < BULLET_SIZE; b++) {
                int px_idx = getPhysPos(projectiles[i].pos + b);
                if (px_idx >= 0 && px_idx < NUM_LEDS) {
                    int total = pixelOccupancy[px_idx];
                    
                    if (total > 1) {
                        // [重叠状态]：轮播闪烁逻辑
                        // 获取当前位置这是第几颗子弹
                        int currentIdx = pixelCurrentIndex[px_idx]++;
                        // 根据时间决定显示哪一颗 (每150ms切换一次)
                        int activeSlot = (millis() / 150) % total; 
                        
                        if (currentIdx == activeSlot) {
                            // 轮到这颗子弹显示：高亮覆盖（使用=而非+=，保证颜色纯正）
                            // 亮度爆发 3 倍，确保醒目
                            int burstBrightness = min(255, INITIAL_BRIGHTNESS * 3);
                            leds[px_idx] = applySoftwareBrightness(projectiles[i].color, burstBrightness);
                        }
                    } else {
                        // [普通状态]：正常叠加
                        // 只有一颗子弹，使用标准亮度
                        leds[px_idx] += applySoftwareBrightness(projectiles[i].color, INITIAL_BRIGHTNESS);
                    }
                }
            }
        }
    }

    // 5. 绘制盾牌 (支持 50 颗全渲染)
    if (current_play_mode == 0) {
        for (int i = 0; i < 50; i++) {
            if (shields[i].active) {
                for (int s = 0; s < SHIELD_SIZE; s++) {
                    int px_idx = getPhysPos(shields[i].pos + s);
                    if (px_idx >= 0 && px_idx < NUM_LEDS) {
                        leds[px_idx] = applySoftwareBrightness(shields[i].color, INITIAL_BRIGHTNESS);
                    }
                }
            }
        }
    }

    // 6. 绘制老虎 (呼吸特效)
    uint8_t breath_val = beatsin8(TIGER_BREATH_SPEED, 150, 255);
    CRGB tiger_base_col;
    
    // 检查老虎是否退无可退（离开起始位置后又返回）
    bool isTigerAtHome = (tigerPos >= INITIAL_TIGER_POS - 1);
    if (tigerLeftHome && isTigerAtHome && homeHitCount > 0) {
        // 老虎退无可退，慢闪红色
        uint8_t flash_val = beatsin8(5, 100, 255); // 慢闪频率
        tiger_base_col = CRGB(flash_val, 0, 0); // 红色
    } else {
        // 正常状态，使用默认颜色
        tiger_base_col = CHSV(millis() / 10, 255, breath_val);
    }
    
    for (int i = 0; i < TIGER_SIZE; i++) {
        int px_idx = getPhysPos(tigerPos + i);
        if (px_idx >= 0 && px_idx < NUM_LEDS) {
            leds[px_idx] = applySoftwareBrightness(tiger_base_col, INITIAL_BRIGHTNESS);
        }
    }
}

// [RETAINED - 老虎归位补全动画]
/**
 * 更新老虎归位动画
 * 处理老虎从当前位置回到起始位置的动画效果
 */
void updateTigerReturn() {
    tigerReturnPos += 0.7;
    if (tigerReturnPos >= INITIAL_TIGER_POS) { 
        tigerPos = INITIAL_TIGER_POS; 
        isReturning = false; 
        showWinEffect = true; 
        effectStartTime = millis(); 
    } else { 
        tigerPos = (int)tigerReturnPos; 
    }
}

// ============================================================
// =====                5. 硬件输入与双机通信 [RETAINED]       =====
// ============================================================

/**
 * 处理硬件输入
 * 读取物理按钮状态并处理输入事件
 */
void handleInputs() {
    static bool lastR_in = 1, lastG_in = 1, lastB_in = 1;
    bool r_state = digitalRead(2), g_state = digitalRead(3), b_state = digitalRead(10);
    
    auto process = [](char cmd_char) {
        if (resolvedRole == 1) { 
            // 从机模式：发送ESP-NOW消息给主机
            myData.command = cmd_char; strcpy(myData.game_id, GAME_ID); 
            esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData)); 
        } else { 
            // 主机模式：直接处理输入
            CRGB actual_c;
            if (cmd_char == 'r') actual_c = CRGB::Red; 
            else if (cmd_char == 'g') actual_c = CRGB::Green; 
            else actual_c = CRGB::Blue;
            checkRhythm(actual_c); 
        }
    };
    
    // 检测按钮按下事件
    if (r_state == 0 && lastR_in == 1) { process('g'); delay(10); }
    if (g_state == 0 && lastG_in == 1) { process('r'); delay(10); }
    if (b_state == 0 && lastB_in == 1) { process('b'); delay(10); }
    
    // 更新按钮状态
    lastR_in = r_state; lastG_in = g_state; lastB_in = b_state;
}

/**
 * 检查节奏模式输入
 * 根据当前游戏模式处理颜色输入
 * @param color 输入的颜色
 */
void checkRhythm(CRGB color) {
    if (getSubMode() == 0) { 
        // 普通模式：生成盾牌
        spawnShield(color); 
    } else {
        // 节奏模式：检查颜色匹配
        int combo_target_val = BASE_RHYTHM_TARGET * (1 + (currentLevel - 1) * 0.2);
        for (int i = 0; i < 25; i++) {
            if (projectiles[i].active && projectiles[i].pos >= JUDGE_ZONE_START && projectiles[i].pos <= (JUDGE_ZONE_START + JUDGE_ZONE_WIDTH)) {
                if (projectiles[i].color == color) {
                    // 颜色匹配成功
                    projectiles[i].active = false; 
                    addEffect("match");
                    comboCounter++;
                    if (comboCounter >= COMBO_TARGET) { 
                        // 触发老虎长休
                        longRestPending = true; 
                    }
                    if (comboCounter >= combo_target_val) { 
                        // 达到节奏模式目标，老虎归位
                        isReturning = true; 
                        tigerReturnPos = tigerPos; 
                        addEffect("win"); 
                    }
                    return;
                }
            }
        }
        // 颜色匹配失败
        addEffect("break");
        comboCounter = 0;
    }
}

/**
 * 生成盾牌
 * 在普通模式下，根据输入颜色生成对应颜色的盾牌
 * @param color 盾牌颜色
 */
void spawnShield(CRGB color) {
    for (int i = 0; i < 50; i++) {
        if (!shields[i].active) {
            shields[i].active = true; 
            shields[i].pos = ELEPHANT_POS + 1.0; 
            shields[i].color = color;
            addEffect("shield"); 
            break;
        }
    }
}

// ============================================================
// =====                6. 网页配置与音效引擎 [🍉RETAINED]     =====
// ============================================================

// [RETAINED - 严格遵循V25最原始 Web Audio 代码，禁简缩写]
/**
 * 处理网页根路径请求
 * 生成包含所有配置选项的网页界面
 */
void handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, shrink-to-fit=no'><meta name='apple-mobile-web-app-capable' content='yes'><meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>";
    h += "<style>body{font-family:sans-serif;background:#f0f0f0;padding:20px;font-size:18px;margin-bottom:120px;user-select:none;} h3{background:#444;color:white;padding:10px;}";
    h += "input,select{width:100%;padding:12px;margin:5px 0;box-sizing:border-box;font-size:16px;} .btn-container{position:fixed;bottom:0;left:0;width:100%;height:100px;display:flex;z-index:1000;}";
    h += ".game-btn{flex:1;border:none;color:white;font-size:24px;font-weight:bold;} .btn-red{background:#f44;} .btn-green{background:#2c7;} .btn-blue{background:#34d;}";
    h += "#audio-mask{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.85);color:white;display:flex;justify-content:center;align-items:center;z-index:999;text-align:center;cursor:pointer;}";
    h += ".top-nav{position:fixed;top:0;left:0;width:100%;background:#333;color:white;padding:10px 20px;display:flex;justify-content:space-between;align-items:center;z-index:998;box-sizing:border-box;}";
    h += ".nav-btn{background:none;border:1px solid white;color:white;padding:5px 10px;border-radius:4px;cursor:pointer;font-size:14px;}";
    h += ".nav-btn:hover{background:rgba(255,255,255,0.2);}";
    h += ".content-wrapper{margin-top:60px;}";
    h += "</style></head><body>";
    h += "<div id='audio-mask' onclick='initAudio()'><div><h1>🔊 点击进入战斗</h1><p>V1.0.0</p></div></div>";
    h += "<div class='top-nav'><button class='nav-btn' onclick='toggleMute()' id='mute-btn'>🔊 静音</button><button class='nav-btn' onclick='toggleLanguage()' id='lang-btn'>English</button></div>";
    h += "<div class='content-wrapper'><h2>参数精调菜单</h2><form action='/save' method='POST'>";
    
    h += "<h3>1. 核心与角色</h3><label>身份模式:</label><select name='role'><option value='2' "+String(DEVICE_ROLE==2?"selected":"" )+">10s扫描竞选(搜Master)</option><option value='0' "+String(DEVICE_ROLE==0?"selected":"" )+">强制主控端</option><option value='1' "+String(DEVICE_ROLE==1?"selected":"" )+">强制从机端</option></select>";
    h += "<label>灯珠总数:</label><input type='number' name='num' value='"+String(NUM_LEDS)+"'><label>基准显示亮度 (推荐40):</label><input type='number' name='bri' value='"+String(INITIAL_BRIGHTNESS)+"'>";
    
    h += "<h3>2. 游戏模式与重力</h3><label>游戏模式:</label><select name='gm'><option value='0' "+String(GAME_MODE==0?"selected":"" )+">普通模式</option><option value='1' "+String(GAME_MODE==1?"selected":"" )+">节奏模式</option><option value='2' "+String(GAME_MODE==2?"selected":"" )+">混合模式</option></select>";
    h += "<label>重力模式:</label><select name='gs'><option value='0' "+String(GRAVITY_SETTING==0?"selected":"" )+">正向</option><option value='1' "+String(GRAVITY_SETTING==1?"selected":"" )+">反向</option><option value='2' "+String(GRAVITY_SETTING==2?"selected":"" )+">自动切换</option></select>";
    h += "<label>关卡上限:</label><input type='number' name='mll' value='"+String(MAX_LEVEL_LIMIT)+"'>";
    
    h += "<h3>3. 波次连发扫射 [NEW]</h3><label>扫射触发概率/关 (%):</label><input type='number' name='bcp' value='"+String(BURST_PROB_INC)+"'><label>连发子弹数量上限:</label><input type='number' name='mbc' value='"+String(BURST_MAX_COUNT)+"'>";

    h += "<h3>4. 难度平衡参数</h3><label>初始速度:</label><input type='number' name='is' value='"+String(INITIAL_SPEED)+"'><label>最大速度:</label><input type='number' name='ms' value='"+String(MAX_SPEED)+"'><label>速度增量/关:</label><input type='number' name='sgp' value='"+String(SPEED_GROWTH_PER_LEVEL)+"'><label>位移分母:</label><input type='number' step='0.1' name='pdiv' value='"+String(PHYSICS_DIVISOR)+"'><label>同屏子弹限额:</label><input type='number' name='mbs' value='"+String(MAX_BULLETS_ON_SCREEN)+"'><label>每关间隔减少(ms):</label><input type='number' name='grpl' value='"+String(GAP_REDUCTION_PER_LEVEL)+"'>";
    
    h += "<h3>5. 老虎动作逻辑</h3><label>普通休息(ms):</label><input type='number' name='rtn' value='"+String(REST_TIME_NORMAL)+"'><label>长休时间(秒):</label><input type='number' name='rtl' value='"+String(REST_TIME_LONG)+"'><label>连击长休目标:</label><input type='number' name='ct' value='"+String(COMBO_TARGET)+"'><label>最小间隔(ms):</label><input type='number' name='gmin' value='"+String(GAP_MIN_MS)+"'><label>最大间隔(ms):</label><input type='number' name='gmax' value='"+String(GAP_MAX_MS)+"'><label>前进步数:</label><input type='number' name='tmf' value='"+String(TIGER_MOVE_FORWARD)+"'><label>后退步数:</label><input type='number' name='tmb' value='"+String(TIGER_MOVE_BACKWARD)+"'><label>漏球触发前移:</label><input type='number' name='ptf' value='"+String(PROJECTILE_TO_FORWARD)+"'><label>击中触发后退:</label><input type='number' name='stb' value='"+String(SHIELD_TO_BACKWARD)+"'><label>连续漏球惩罚翻倍值:</label><input type='number' name='ctd' value='"+String(COMBO_TO_DOUBLE)+"'>";
    
    h += "<h3>6. 尺寸与判定</h3><label>子弹长度:</label><input type='number' name='bs' value='"+String(BULLET_SIZE)+"'><label>盾牌长度:</label><input type='number' name='ss' value='"+String(SHIELD_SIZE)+"'><label>老虎长度:</label><input type='number' name='ts' value='"+String(TIGER_SIZE)+"'><label>判定区起始:</label><input type='number' name='jzs' value='"+String(JUDGE_ZONE_START)+"'><label>判定区宽度:</label><input type='number' name='jzw' value='"+String(JUDGE_ZONE_WIDTH)+"'><label>判定区亮度:</label><input type='number' name='jzb' value='"+String(JUDGE_ZONE_BRIGHTNESS)+"'><label>判定区颜色 R:</label><input type='number' name='jcr' value='"+String(JUDGE_ZONE_COLOR_R)+"'><label>判定区颜色 G:</label><input type='number' name='jcg' value='"+String(JUDGE_ZONE_COLOR_G)+"'><label>判定区颜色 B:</label><input type='number' name='jcb' value='"+String(JUDGE_ZONE_COLOR_B)+"'>";
    
    h += "<h3>7. 动画与效果</h3><label>老虎呼吸频率:</label><input type='number' name='tbs' value='"+String(TIGER_BREATH_SPEED)+"'><label>特效呼吸频率:</label><input type='number' name='ebs' value='"+String(EFFECT_BREATH_SPEED)+"'><label>特效持续时间(ms):</label><input type='number' name='ed' value='"+String(EFFECT_DURATION)+"'>";
    
    h += "<h3>8. 节奏模式设置</h3><label>节奏模式目标击中数/关:</label><input type='number' name='brt' value='"+String(BASE_RHYTHM_TARGET)+"'><label>胜利所需击中数:</label><input type='number' name='whr' value='"+String(WIN_HIT_REQUIRED)+"'><label>每关增加击中数:</label><input type='number' name='hipl' value='"+String(HIT_INCREASE_PER_LEVEL)+"'>";
    
    h += "<input type='submit' style='background:orange;height:50px;margin-top:20px;color:white;font-weight:bold;' value='保存全部参数并重启'></form></div>";
    h += "<div class='btn-container'><button class='game-btn btn-blue' onclick='fetch(\"/btn?c=b\")'>蓝</button><button class='game-btn btn-red' onclick='fetch(\"/btn?c=r\")'>红</button><button class='game-btn btn-green' onclick='fetch(\"/btn?c=g\")'>绿</button></div>";
    
    // [RESTORED] 🍉 逐字还原 melody, bass 及所有波形合成 JS 代码，严禁精简
    h += "<script>let audioCtx; let isMuted = false; let currentLanguage = 'zh'; let lastTap = 0; let touchStartY = 0; let touchStartX = 0;"; h += "document.addEventListener('touchstart', function(e){ touchStartX = e.touches[0].clientX; touchStartY = e.touches[0].clientY; const now = new Date().getTime(); const tapLength = now - lastTap; if(tapLength < 300 && tapLength > 0){ e.preventDefault(); } lastTap = now; }, { passive: false });"; h += "document.addEventListener('touchmove', function(e){ if(e.touches.length > 1){ e.preventDefault(); } }, { passive: false });";
    h += "const translations = {";
    h += "zh: {";
    h += "menuTitle: '参数精调菜单',";
    h += "coreSection: '1. 核心与角色',";
    h += "roleMode: '身份模式:',";
    h += "roleOption1: '10s扫描竞选(搜Master)',";
    h += "roleOption2: '强制主控端',";
    h += "roleOption3: '强制从机端',";
    h += "ledCount: '灯珠总数:',";
    h += "brightness: '基准显示亮度 (推荐40):',";
    h += "gameModeSection: '2. 游戏模式与重力',";
    h += "gameMode: '游戏模式:',";
    h += "gameModeOption1: '普通模式',";
    h += "gameModeOption2: '节奏模式',";
    h += "gameModeOption3: '混合模式',";
    h += "gravityMode: '重力模式:',";
    h += "gravityOption1: '正向',";
    h += "gravityOption2: '反向',";
    h += "gravityOption3: '自动切换',";
    h += "maxLevel: '关卡上限:',";
    h += "burstSection: '3. 波次连发扫射 [NEW]',";
    h += "burstProb: '扫射触发概率/关 (%):',";
    h += "burstMax: '连发子弹数量上限:',";
    h += "difficultySection: '4. 难度平衡参数',";
    h += "initialSpeed: '初始速度:',";
    h += "maxSpeed: '最大速度:',";
    h += "speedGrowth: '速度增量/关:',";
    h += "physicsDivisor: '位移分母:',";
    h += "maxBullets: '同屏子弹限额:',";
    h += "gapReduction: '每关间隔减少(ms):',";
    h += "tigerSection: '5. 老虎动作逻辑',";
    h += "restNormal: '普通休息(ms):',";
    h += "restLong: '长休时间(秒):',";
    h += "comboTarget: '连击长休目标:',";
    h += "gapMin: '最小间隔(ms):',";
    h += "gapMax: '最大间隔(ms):',";
    h += "tigerForward: '前进步数:',";
    h += "tigerBackward: '后退步数:',";
    h += "projectileForward: '漏球触发前移:',";
    h += "shieldBackward: '击中触发后退:',";
    h += "comboDouble: '连续漏球惩罚翻倍值:',";
    h += "sizeSection: '6. 尺寸与判定',";
    h += "bulletSize: '子弹长度:',";
    h += "shieldSize: '盾牌长度:',";
    h += "tigerSize: '老虎长度:',";
    h += "judgeStart: '判定区起始:',";
    h += "judgeWidth: '判定区宽度:',";
    h += "judgeBrightness: '判定区亮度:',";
    h += "judgeColorR: '判定区颜色 R:',";
    h += "judgeColorG: '判定区颜色 G:',";
    h += "judgeColorB: '判定区颜色 B:',";
    h += "animationSection: '7. 动画与效果',";
    h += "tigerBreath: '老虎呼吸频率:',";
    h += "effectBreath: '特效呼吸频率:',";
    h += "effectDuration: '特效持续时间(ms):',";
    h += "rhythmSection: '8. 节奏模式设置',";
    h += "rhythmTarget: '节奏模式目标击中数/关:',";
    h += "winHits: '胜利所需击中数:',";
    h += "hitIncrease: '每关增加击中数:',";
    h += "saveButton: '保存全部参数并重启',";
    h += "btnBlue: '蓝',";
    h += "btnRed: '红',";
    h += "btnGreen: '绿',";
    h += "muteButton: '🔊 静音',";
    h += "langButton: 'English',";
    h += "audioMaskTitle: '🔊 点击进入战斗',";
    h += "audioMaskSubtitle: 'V28.0 2叠限制+扫射攻击重型版'";
    h += "},";
    h += "en: {";
    h += "menuTitle: 'Parameter Tuning Menu',";
    h += "coreSection: '1. Core & Role',";
    h += "roleMode: 'Identity Mode:',";
    h += "roleOption1: '10s Scan Election (Search Master)',";
    h += "roleOption2: 'Force Master',";
    h += "roleOption3: 'Force Slave',";
    h += "ledCount: 'Total LEDs:',";
    h += "brightness: 'Base Brightness (Recommended 40):',";
    h += "gameModeSection: '2. Game Mode & Gravity',";
    h += "gameMode: 'Game Mode:',";
    h += "gameModeOption1: 'Normal Mode',";
    h += "gameModeOption2: 'Rhythm Mode',";
    h += "gameModeOption3: 'Mixed Mode',";
    h += "gravityMode: 'Gravity Mode:',";
    h += "gravityOption1: 'Normal',";
    h += "gravityOption2: 'Inverted',";
    h += "gravityOption3: 'Auto Switch',";
    h += "maxLevel: 'Max Level Limit:',";
    h += "burstSection: '3. Burst Fire [NEW]',";
    h += "burstProb: 'Burst Trigger Probability/Level (%):',";
    h += "burstMax: 'Max Burst Count:',";
    h += "difficultySection: '4. Difficulty Balance',";
    h += "initialSpeed: 'Initial Speed:',";
    h += "maxSpeed: 'Max Speed:',";
    h += "speedGrowth: 'Speed Growth/Level:',";
    h += "physicsDivisor: 'Physics Divisor:',";
    h += "maxBullets: 'Max On-Screen Bullets:',";
    h += "gapReduction: 'Gap Reduction/Level (ms):',";
    h += "tigerSection: '5. Tiger Logic',";
    h += "restNormal: 'Normal Rest (ms):',";
    h += "restLong: 'Long Rest (seconds):',";
    h += "comboTarget: 'Combo Long Rest Target:',";
    h += "gapMin: 'Min Gap (ms):',";
    h += "gapMax: 'Max Gap (ms):',";
    h += "tigerForward: 'Forward Steps:',";
    h += "tigerBackward: 'Backward Steps:',";
    h += "projectileForward: 'Projectile Hit Forward:',";
    h += "shieldBackward: 'Shield Hit Backward:',";
    h += "comboDouble: 'Combo Penalty Double Threshold:',";
    h += "sizeSection: '6. Size & Judgement',";
    h += "bulletSize: 'Bullet Size:',";
    h += "shieldSize: 'Shield Size:',";
    h += "tigerSize: 'Tiger Size:',";
    h += "judgeStart: 'Judge Zone Start:',";
    h += "judgeWidth: 'Judge Zone Width:',";
    h += "judgeBrightness: 'Judge Zone Brightness:',";
    h += "judgeColorR: 'Judge Zone Color R:',";
    h += "judgeColorG: 'Judge Zone Color G:',";
    h += "judgeColorB: 'Judge Zone Color B:',";
    h += "animationSection: '7. Animation & Effects',";
    h += "tigerBreath: 'Tiger Breath Speed:',";
    h += "effectBreath: 'Effect Breath Speed:',";
    h += "effectDuration: 'Effect Duration (ms):',";
    h += "rhythmSection: '8. Rhythm Mode Settings',";
    h += "rhythmTarget: 'Rhythm Mode Target Hits/Level:',";
    h += "winHits: 'Win Required Hits:',";
    h += "hitIncrease: 'Hit Increase/Level:',";
    h += "saveButton: 'Save All Parameters & Restart',";
    h += "btnBlue: 'Blue',";
    h += "btnRed: 'Red',";
    h += "btnGreen: 'Green',";
    h += "muteButton: '🔇 Unmute',";
    h += "langButton: '中文',";
    h += "audioMaskTitle: '🔊 Click to Enter Battle',";
    h += "audioMaskSubtitle: 'V28.0 2-Stack Limit + Burst Attack Heavy Version'";
    h += "}";
    h += "};";
    h += "function initAudio(){ audioCtx = new (window.AudioContext || window.webkitAudioContext)(); document.getElementById('audio-mask').style.display='none'; playBGM(); poll(); }";
    h += "function toggleMute(){ isMuted = !isMuted; document.getElementById('mute-btn').textContent = isMuted ? '🔇 取消静音' : '🔊 静音'; }";
    h += "function toggleLanguage(){ currentLanguage = currentLanguage === 'zh' ? 'en' : 'zh'; updateLanguage(); document.getElementById('lang-btn').textContent = currentLanguage === 'zh' ? 'English' : '中文'; }";
    h += "function updateLanguage(){";
    h += "const lang = translations[currentLanguage];";
    h += "document.querySelector('.content-wrapper h2').textContent = lang.menuTitle;";
    h += "document.querySelectorAll('h3')[0].textContent = lang.coreSection;";
    h += "document.querySelectorAll('label')[0].textContent = lang.roleMode;";
    h += "document.querySelectorAll('select[name=role] option')[0].textContent = lang.roleOption1;";
    h += "document.querySelectorAll('select[name=role] option')[1].textContent = lang.roleOption2;";
    h += "document.querySelectorAll('select[name=role] option')[2].textContent = lang.roleOption3;";
    h += "document.querySelectorAll('label')[1].textContent = lang.ledCount;";
    h += "document.querySelectorAll('label')[2].textContent = lang.brightness;";
    h += "document.querySelectorAll('h3')[1].textContent = lang.gameModeSection;";
    h += "document.querySelectorAll('label')[3].textContent = lang.gameMode;";
    h += "document.querySelectorAll('select[name=gm] option')[0].textContent = lang.gameModeOption1;";
    h += "document.querySelectorAll('select[name=gm] option')[1].textContent = lang.gameModeOption2;";
    h += "document.querySelectorAll('select[name=gm] option')[2].textContent = lang.gameModeOption3;";
    h += "document.querySelectorAll('label')[4].textContent = lang.gravityMode;";
    h += "document.querySelectorAll('select[name=gs] option')[0].textContent = lang.gravityOption1;";
    h += "document.querySelectorAll('select[name=gs] option')[1].textContent = lang.gravityOption2;";
    h += "document.querySelectorAll('select[name=gs] option')[2].textContent = lang.gravityOption3;";
    h += "document.querySelectorAll('label')[5].textContent = lang.maxLevel;";
    h += "document.querySelectorAll('h3')[2].textContent = lang.burstSection;";
    h += "document.querySelectorAll('label')[6].textContent = lang.burstProb;";
    h += "document.querySelectorAll('label')[7].textContent = lang.burstMax;";
    h += "document.querySelectorAll('h3')[3].textContent = lang.difficultySection;";
    h += "document.querySelectorAll('label')[8].textContent = lang.initialSpeed;";
    h += "document.querySelectorAll('label')[9].textContent = lang.maxSpeed;";
    h += "document.querySelectorAll('label')[10].textContent = lang.speedGrowth;";
    h += "document.querySelectorAll('label')[11].textContent = lang.physicsDivisor;";
    h += "document.querySelectorAll('label')[12].textContent = lang.maxBullets;";
    h += "document.querySelectorAll('label')[13].textContent = lang.gapReduction;";
    h += "document.querySelectorAll('h3')[4].textContent = lang.tigerSection;";
    h += "document.querySelectorAll('label')[14].textContent = lang.restNormal;";
    h += "document.querySelectorAll('label')[15].textContent = lang.restLong;";
    h += "document.querySelectorAll('label')[16].textContent = lang.comboTarget;";
    h += "document.querySelectorAll('label')[17].textContent = lang.gapMin;";
    h += "document.querySelectorAll('label')[18].textContent = lang.gapMax;";
    h += "document.querySelectorAll('label')[19].textContent = lang.tigerForward;";
    h += "document.querySelectorAll('label')[20].textContent = lang.tigerBackward;";
    h += "document.querySelectorAll('label')[21].textContent = lang.projectileForward;";
    h += "document.querySelectorAll('label')[22].textContent = lang.shieldBackward;";
    h += "document.querySelectorAll('label')[23].textContent = lang.comboDouble;";
    h += "document.querySelectorAll('h3')[5].textContent = lang.sizeSection;";
    h += "document.querySelectorAll('label')[24].textContent = lang.bulletSize;";
    h += "document.querySelectorAll('label')[25].textContent = lang.shieldSize;";
    h += "document.querySelectorAll('label')[26].textContent = lang.tigerSize;";
    h += "document.querySelectorAll('label')[27].textContent = lang.judgeStart;";
    h += "document.querySelectorAll('label')[28].textContent = lang.judgeWidth;";
    h += "document.querySelectorAll('label')[29].textContent = lang.judgeBrightness;";
    h += "document.querySelectorAll('label')[30].textContent = lang.judgeColorR;";
    h += "document.querySelectorAll('label')[31].textContent = lang.judgeColorG;";
    h += "document.querySelectorAll('label')[32].textContent = lang.judgeColorB;";
    h += "document.querySelectorAll('h3')[6].textContent = lang.animationSection;";
    h += "document.querySelectorAll('label')[33].textContent = lang.tigerBreath;";
    h += "document.querySelectorAll('label')[34].textContent = lang.effectBreath;";
    h += "document.querySelectorAll('label')[35].textContent = lang.effectDuration;";
    h += "document.querySelectorAll('h3')[7].textContent = lang.rhythmSection;";
    h += "document.querySelectorAll('label')[36].textContent = lang.rhythmTarget;";
    h += "document.querySelectorAll('label')[37].textContent = lang.winHits;";
    h += "document.querySelectorAll('label')[38].textContent = lang.hitIncrease;";
    h += "document.querySelector('input[type=submit]').value = lang.saveButton;";
    h += "document.querySelectorAll('.game-btn')[0].textContent = lang.btnBlue;";
    h += "document.querySelectorAll('.game-btn')[1].textContent = lang.btnRed;";
    h += "document.querySelectorAll('.game-btn')[2].textContent = lang.btnGreen;";
    h += "document.getElementById('mute-btn').textContent = isMuted ? '🔇 取消静音' : '🔊 静音';";
    h += "document.querySelector('#audio-mask h1').textContent = lang.audioMaskTitle;";
    h += "document.querySelector('#audio-mask p').textContent = lang.audioMaskSubtitle;";
    h += "}";
    h += "function playNote(freq, type, start, duration, vol){ if(freq === 0 || !audioCtx || isMuted) return; const osc = audioCtx.createOscillator(); const g = audioCtx.createGain();";
    h += "osc.type = type; osc.frequency.value = freq; g.gain.setValueAtTime(vol, start); g.gain.exponentialRampToValueAtTime(0.001, start + duration);";
    h += "osc.connect(g); g.connect(audioCtx.destination); osc.start(start); osc.stop(start + duration); }";
    h += "function playBGM(){ const tempo = 0.125; let step = 0;";
    h += "const melody = [349,0,349,349,392,440,0,466,523,0,523,523,466,440,392,0, 349,0,349,349,392,440,0,466,587,523,466,392,349,0,0,0];";
    h += "const bass = [174,174,174,174,196,196,196,196,220,220,220,220,233,233,233,233, 174,174,174,174,196,196,196,196,146,146,146,146,174,174,174,174];";
    h += "setInterval(() => { if(!audioCtx || isMuted) return; const now = audioCtx.currentTime;";
    h += "playNote(melody[step], 'square', now, tempo * 0.9, 0.05);";
    h += "playNote(bass[step], 'triangle', now, tempo * 0.8, 0.08);";
    h += "step = (step + 1) % melody.length; }, tempo * 1000); }";
    h += "function playSound(type){ if(!audioCtx || isMuted) return; const now = audioCtx.currentTime;";
    h += "switch(type){";
    h += "case 'shield': playNote(800, 'sine', now, 0.1, 0.2); break;";
    h += "case 'break': playNote(120, 'sawtooth', now, 0.15, 0.3); break;";
    h += "case 'match': playNote(600, 'square', now, 0.06, 0.15); break;";
    h += "case 'hit_tiger': playNote(400, 'square', now, 0.2, 0.2); break;";
    h += "case 'hit_ele': playNote(100, 'square', now, 0.1, 0.4); break;";
    h += "case 'eat': playNote(200, 'sawtooth', now, 0.4, 0.5); break;";
    h += "case 'win': [523, 659, 783, 1046].forEach((f,i)=>playNote(f, 'sine', now+i*0.1, 0.15, 0.2)); break;";
    h += "case 'lose': playNote(200, 'square', now, 0.6, 0.3); break;";
    h += "}} async function poll(){ try{ const r=await fetch('/events'); const t=await r.text(); if(t)t.split(',').forEach(s=>playSound(s)); }catch(e){} setTimeout(poll,150); }";
    h += "</script></body></html>";
    server.send(200, "text/html", h);
}

// [RETAINED - 完全补全的 38 参数持久化读写逻辑]
/**
 * 处理保存配置请求
 * 将网页提交的配置参数保存到非易失性存储
 */
void handleSave() {
    preferences.begin("game_cfg", false);
    if(server.hasArg("role")) preferences.putInt("role", server.arg("role").toInt());
    if(server.hasArg("num"))  preferences.putInt("num", server.arg("num").toInt());
    if(server.hasArg("bri"))  preferences.putInt("bri", server.arg("bri").toInt());
    if(server.hasArg("gm"))   preferences.putInt("gm", server.arg("gm").toInt());
    if(server.hasArg("gs"))   preferences.putInt("gs", server.arg("gs").toInt());
    if(server.hasArg("mll"))  preferences.putInt("mll", server.arg("mll").toInt());
    if(server.hasArg("is"))   preferences.putInt("is", server.arg("is").toInt());
    if(server.hasArg("ms"))   preferences.putInt("ms", server.arg("ms").toInt());
    if(server.hasArg("sgp"))  preferences.putInt("sgp", server.arg("sgp").toInt());
    if(server.hasArg("pdiv")) preferences.putFloat("pdiv", server.arg("pdiv").toFloat());
    if(server.hasArg("mbs"))  preferences.putInt("mbs", server.arg("mbs").toInt());
    if(server.hasArg("grpl")) preferences.putInt("grpl", server.arg("grpl").toInt());
    if(server.hasArg("gmin")) preferences.putInt("gmin", server.arg("gmin").toInt());
    if(server.hasArg("gmax")) preferences.putInt("gmax", server.arg("gmax").toInt());
    if(server.hasArg("tmf"))  preferences.putInt("tmf", server.arg("tmf").toInt());
    if(server.hasArg("tmb"))  preferences.putInt("tmb", server.arg("tmb").toInt());
    if(server.hasArg("ptf"))  preferences.putInt("ptf", server.arg("ptf").toInt());
    if(server.hasArg("stb"))  preferences.putInt("stb", server.arg("stb").toInt());
    if(server.hasArg("ctd"))  preferences.putInt("ctd", server.arg("ctd").toInt());
    if(server.hasArg("bs"))   preferences.putInt("bs", server.arg("bs").toInt());
    if(server.hasArg("ss"))   preferences.putInt("ss", server.arg("ss").toInt());
    if(server.hasArg("ts"))   preferences.putInt("ts", server.arg("ts").toInt());
    if(server.hasArg("jzs"))  preferences.putInt("jzs", server.arg("jzs").toInt());
    if(server.hasArg("jzw"))  preferences.putInt("jzw", server.arg("jzw").toInt());
    if(server.hasArg("jzb"))  preferences.putInt("jzb", server.arg("jzb").toInt());
    if(server.hasArg("jcr"))  preferences.putInt("jcr", server.arg("jcr").toInt());
    if(server.hasArg("jcg"))  preferences.putInt("jcg", server.arg("jcg").toInt());
    if(server.hasArg("jcb"))  preferences.putInt("jcb", server.arg("jcb").toInt());
    if(server.hasArg("rtn"))  preferences.putInt("rtn", server.arg("rtn").toInt());
    if(server.hasArg("rtl"))  preferences.putInt("rtl", server.arg("rtl").toInt());
    if(server.hasArg("ct"))   preferences.putInt("ct", server.arg("ct").toInt());
    if(server.hasArg("bcp"))  preferences.putInt("bcp", server.arg("bcp").toInt());
    if(server.hasArg("mbc"))  preferences.putInt("mbc", server.arg("mbc").toInt());
    if(server.hasArg("tbs"))  preferences.putInt("tbs", server.arg("tbs").toInt());
    if(server.hasArg("ebs"))  preferences.putInt("ebs", server.arg("ebs").toInt());
    if(server.hasArg("ed"))   preferences.putInt("ed", server.arg("ed").toInt());
    if(server.hasArg("brt"))  preferences.putInt("brt", server.arg("brt").toInt());
    if(server.hasArg("whr"))  preferences.putInt("whr", server.arg("whr").toInt());
    if(server.hasArg("hipl")) preferences.putInt("hipl", server.arg("hipl").toInt()); // [NEW] 保存每关增加的击中数
    preferences.end();
    
    // 返回包含自动跳转脚本的 HTML 页面
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{font-family:sans-serif;text-align:center;padding:50px;background:#f0f0f0;}";
    html += ".card{background:white;padding:30px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
    html += ".loader{border:5px solid #f3f3f3;border-top:5px solid #3498db;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:20px auto;}";
    html += "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}";
    html += "button{background:#3498db;color:white;border:none;padding:10px 20px;border-radius:5px;font-size:16px;cursor:pointer;margin-top:20px;}";
    html += "</style></head><body>";
    html += "<div class='card'><h1>参数已保存</h1>";
    html += "<p>设备正在重启，WiFi将会断开...</p>";
    html += "<div class='loader'></div>";
    html += "<p id='status'>正在等待重连...</p>";
    html += "<button onclick='window.location.href=\"/\"'>手动返回主页</button></div>";
    html += "<script>";
    html += "let checkCount = 0;";
    html += "function checkServer() {";
    html += "  document.getElementById('status').innerText = '正在尝试连接设备... (' + (++checkCount) + ')';";
    html += "  fetch('/').then(response => {";
    html += "    if(response.ok) { window.location.href = '/'; }";
    html += "  }).catch(e => {";
    html += "    console.log('Waiting for server...');";
    html += "    setTimeout(checkServer, 2000);"; // 每2秒检测一次
    html += "  });";
    html += "}";
    html += "setTimeout(checkServer, 5000);"; // 5秒后开始检测
    html += "</script></body></html>";

    server.send(200, "text/html", html);
    delay(1000); 
    ESP.restart();
}

/**
 * 处理ESP-NOW数据接收
 * 接收从机发送的命令并处理
 * @param info 接收信息
 * @param incomingData 接收到的数据
 * @param len 数据长度
 */
void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingData, int len) {
    if (resolvedRole != 0) return; // 只在主机模式下处理
    
    struct_message *received = (struct_message *)incomingData;
    if (strcmp(received->game_id, GAME_ID) == 0) {
        // 匹配游戏ID，处理命令
        CRGB c_col_map;
        if (received->command == 'r') c_col_map = CRGB::Red; 
        else if (received->command == 'g') c_col_map = CRGB::Green; 
        else c_col_map = CRGB::Blue;
        checkRhythm(c_col_map);
    }
}

// ============================================================
// =====                7. 系统核心启动初始化逻辑             =====
// ============================================================

/**
 * 系统初始化设置
 * 包括硬件初始化、配置加载、角色选举、网络设置等
 */
void setup() {
    Serial.begin(115200);
    Serial.println("\n========================================");
    Serial.println("=== LED Game System Initialization ===");
    Serial.println("========================================");
    
    // 初始化硬件输入引脚
    pinMode(2, INPUT_PULLUP); 
    pinMode(3, INPUT_PULLUP); 
    pinMode(10, INPUT_PULLUP);
    Serial.println("[LOG] Hardware pins initialized");

    // [watermelon] 补足 Preferences 参数读取，确保与网页完全对齐
    preferences.begin("game_cfg", true); 
    NUM_LEDS = preferences.getInt("num", 120);
    DEVICE_ROLE = preferences.getInt("role", 2);
    INITIAL_BRIGHTNESS = preferences.getInt("bri", 40);
    GAME_MODE = preferences.getInt("gm", 0);
    GRAVITY_SETTING = preferences.getInt("gs", 2);
    MAX_LEVEL_LIMIT = preferences.getInt("mll", 10);
    INITIAL_SPEED = preferences.getInt("is", 5);
    MAX_SPEED = preferences.getInt("ms", 100);
    SPEED_GROWTH_PER_LEVEL = preferences.getInt("sgp", 4);
    MAX_BULLETS_ON_SCREEN = preferences.getInt("mbs", 6);
    GAP_MIN_MS = preferences.getInt("gmin", 1200);
    GAP_MAX_MS = preferences.getInt("gmax", 2500);
    GAP_REDUCTION_PER_LEVEL = preferences.getInt("grpl", 200);
    PHYSICS_DIVISOR = preferences.getFloat("pdiv", 80.0);
    TIGER_MOVE_FORWARD = preferences.getInt("tmf", 1);
    TIGER_MOVE_BACKWARD = preferences.getInt("tmb", 3);
    PROJECTILE_TO_FORWARD = preferences.getInt("ptf", 1);
    SHIELD_TO_BACKWARD = preferences.getInt("stb", 1);
    COMBO_TO_DOUBLE = preferences.getInt("ctd", 5);
    BULLET_SIZE = preferences.getInt("bs", 1);
    SHIELD_SIZE = preferences.getInt("ss", 1);
    TIGER_SIZE = preferences.getInt("ts", 2);
    JUDGE_ZONE_START = preferences.getInt("jzs", 4);
    JUDGE_ZONE_WIDTH = preferences.getInt("jzw", 4);
    JUDGE_ZONE_BRIGHTNESS = preferences.getInt("jzb", 120);
    JUDGE_ZONE_COLOR_R = preferences.getInt("jcr", 120);
    JUDGE_ZONE_COLOR_G = preferences.getInt("jcg", 120);
    JUDGE_ZONE_COLOR_B = preferences.getInt("jcb", 120);
    REST_TIME_NORMAL = preferences.getInt("rtn", 3000);
    REST_TIME_LONG = preferences.getInt("rtl", 10);
    COMBO_TARGET = preferences.getInt("ct", 8);
    BURST_PROB_INC = preferences.getInt("bcp", 5);
    BURST_MAX_COUNT = preferences.getInt("mbc", 5);
    TIGER_BREATH_SPEED = preferences.getInt("tbs", 10);
    EFFECT_BREATH_SPEED = preferences.getInt("ebs", 15);
    EFFECT_DURATION = preferences.getInt("ed", 5000);
    BASE_RHYTHM_TARGET = preferences.getInt("brt", 5);
    WIN_HIT_REQUIRED = preferences.getInt("whr", 2);
    // [NEW] 读取每关增加的击中数，默认为2
    HIT_INCREASE_PER_LEVEL = preferences.getInt("hipl", 2); 
    preferences.end();
    
    // 输出配置信息
    Serial.println("[LOG] Configuration loaded:");
    Serial.printf("[LOG] LEDs: %d, Brightness: %d\n", NUM_LEDS, INITIAL_BRIGHTNESS);
    Serial.printf("[LOG] Device Role: %d, Game Mode: %d\n", DEVICE_ROLE, GAME_MODE);
    Serial.printf("[LOG] Current Level: %d, Max Levels: %d\n", currentLevel, MAX_LEVEL_LIMIT);
    Serial.printf("[LOG] Initial Speed: %d, Speed Growth: %d\n", INITIAL_SPEED, SPEED_GROWTH_PER_LEVEL);
    Serial.printf("[LOG] Burst Probability: %d%%, Max Burst: %d\n", BURST_PROB_INC, BURST_MAX_COUNT);

    // 初始化FastLED
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);
    
    // [NEW] 硬件总闸锁死 255。所有物体的亮度都在 drawGame 中软件缩放
    FastLED.setBrightness(255); 
    FastLED.clear(); 
    FastLED.show();
    Serial.println("[LOG] FastLED initialized with brightness locked to 255");

    // [RETAINED - 🍉 10秒 广谱自动扫描逻辑]
    if (DEVICE_ROLE == 2) {
        Serial.println("[LOG] Starting role election process...");
        WiFi.mode(WIFI_STA); WiFi.disconnect();
        randomSeed(analogRead(0));
        delay(random(1000, 3000)); 
        unsigned long scanStart_ms = millis();
        bool bossFound = false;
        while (millis() - scanStart_ms < 10000) { 
            int n = WiFi.scanNetworks();
            Serial.printf("[LOG] Scanning for master... Found %d networks\n", n);
            for (int i = 0; i < n; i++) {
                if (WiFi.SSID(i).startsWith("LED_Game_Master")) { 
                    bossFound = true; break; 
                }
            }
            if (bossFound) break;
            delay(1000); 
        }
        resolvedRole = bossFound ? 1 : 0;
        Serial.printf("[LOG] Role election complete: %s\n", resolvedRole == 0 ? "Master" : "Slave");
    } else { 
        resolvedRole = DEVICE_ROLE;
        Serial.printf("[LOG] Forced role: %s\n", resolvedRole == 0 ? "Master" : "Slave");
    }

    // 初始化WiFi AP
    WiFi.mode(WIFI_AP_STA);
    if (resolvedRole == 0) strcpy(dynamic_ssid, "LED_Game_Master");
    else strcpy(dynamic_ssid, "LED_Game_Slave");
    
    WiFi.softAP(dynamic_ssid, "835125257", 1);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[LOG] WiFi AP started: %s\n", dynamic_ssid);

    // 初始化ESP-NOW通信
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
        esp_now_peer_info_t peer_info = {}; 
        memcpy(peer_info.peer_addr, broadcastAddress, 6);
        peer_info.channel = 1; 
        peer_info.encrypt = false; 
        esp_now_add_peer(&peer_info);
        Serial.println("[LOG] ESP-NOW initialized successfully");
    } else {
        Serial.println("[ERROR] ESP-NOW initialization failed");
    }

    // 初始化Web服务器
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/events", [](){ server.send(200, "text/plain", eventQueue); eventQueue=""; });
    server.on("/btn", [](){
        if(server.hasArg("c")){
            char c_input = server.arg("c")[0];
            if(resolvedRole == 1){ 
                // 从机模式：发送ESP-NOW消息给主机
                myData.command = c_input; strcpy(myData.game_id, GAME_ID);
                esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
            } else { 
                // 主机模式：直接处理输入
                CRGB c_web_map;
                if (c_input == 'r') c_web_map = CRGB::Red; else if (c_input == 'g') c_web_map = CRGB::Green; else c_web_map = CRGB::Blue;
                checkRhythm(c_web_map); 
            }
        }
        server.send(200, "text/plain", "ok");
    });
    server.begin(); 
    Serial.println("[LOG] Web server initialized on port 80");
    
    // 重置游戏状态
    resetGame();
    Serial.println("[LOG] Game system initialized and ready");
    Serial.println("========================================");
}

/**
 * 显示全局胜负特效
 * 根据当前胜负状态显示对应颜色的呼吸特效
 */
void displayGlobalEffect() {
    uint8_t effect_breath = beatsin8(EFFECT_BREATH_SPEED, 50, 255);
    CRGB effect_base = showWinEffect ? CRGB::Green : CRGB::Red;
    int current_scaled_bri = (INITIAL_BRIGHTNESS * effect_breath) / 255;
    fill_solid(leds, NUM_LEDS, applySoftwareBrightness(effect_base, current_scaled_bri));
    FastLED.show();
}

/**
 * 主游戏循环
 * 处理服务器客户端请求、游戏状态更新、物理引擎、渲染等
 */
void loop() {
    static unsigned long lastStatusLog = 0;
    static unsigned long lastGameEventLog = 0;
    
    // 处理Web服务器客户端请求
    server.handleClient();
    
    // 从机模式：只处理输入并转发给主机
    if (resolvedRole == 1) { 
        handleInputs(); 
        FastLED.clear(); 
        FastLED.show(); 
        delay(5); 
        return; 
    }

    unsigned long lp_now = millis();
    
    // 定时输出游戏状态日志（每2秒）
    if (lp_now - lastStatusLog > 2000) {
        int activeBullets = 0;
        int activeShields = 0;
        for (int i = 0; i < 25; i++) if (projectiles[i].active) activeBullets++;
        for (int i = 0; i < 50; i++) if (shields[i].active) activeShields++;
        
        Serial.printf("[STATUS] Level: %d, Mode: %d, TigerPos: %.1f, Combo: %d\n", 
                     currentLevel, getSubMode(), tigerPos, comboCounter);
        Serial.printf("[STATUS] Active: Bullets=%d, Shields=%d, BurstLeft=%d\n", 
                     activeBullets, activeShields, burstProjectilesLeft);
        Serial.printf("[STATUS] State: Rest=%d, Return=%d, Gravity=%d\n", 
                     isResting, isReturning, isGravityInverted);
        lastStatusLog = lp_now;
    }
    
    // 处理胜负特效
    if (showWinEffect || showLoseEffect) {
        displayGlobalEffect();
        if (lp_now - effectStartTime > EFFECT_DURATION) {
            if (showWinEffect) { 
                // 胜利：升级关卡
                Serial.printf("[EVENT] Victory! Level up to %d\n", currentLevel + 1);
                currentLevel++;
                if (currentLevel > MAX_LEVEL_LIMIT) {
                    currentLevel = 1;
                    Serial.println("[EVENT] Level reset to 1");
                }
            } else {
                // 失败：重置游戏
                Serial.println("[EVENT] Defeat! Game reset");
            }
            resetGame();
            Serial.println("[EVENT] Game reset completed");
        }
        return;
    }

    // [RETAINED - 🍉 50FPS 稳定时钟步长]
    if (lp_now - lastUpdate > 20) {
        if (isReturning) {
            // 老虎归位动画
            updateTigerReturn(); 
        } else { 
            // 正常游戏逻辑
            handleTigerLogic(); 
            updatePhysics(); 
        }
        // 绘制游戏画面
        drawGame(); 
        FastLED.show(); 
        lastUpdate = lp_now;
    }
}
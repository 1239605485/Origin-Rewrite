#include "or_broadcast.h"
#include "or_config.h"
#include "or_rules.h"

#include "or_log.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

/* One row is one voice, never a shared monster template.  The four arrival
 * slots implement T1/T2/T3/T4; death and defeat slots implement first / again
 * / habitual and P1 / P2 / P3+ respectively. */
typedef struct OR_BossVoice {
    uint32_t id;
    const char *arrival[4];
    const char *half;
    const char *death[3];
    const char *defeat[3];
} OR_BossVoice;

#define VOICE(ID, A1, A2, A3, A4, H, D1, D2, D3, P1, P2, P3) \
    { ID, { A1, A2, A3, A4 }, H, { D1, D2, D3 }, { P1, P2, P3 } }

static const OR_BossVoice g_boss_voices[] = {
    VOICE(50u, "咕噜……王冠选中了朕；今天，整片土地都是朕的王座。", "朕又来了。把路擦干净，王冠不喜欢泥。", "你竟还敢应召？很好，朕正缺一个鼓掌的臣民。", "别数了，朕的登基从不嫌多。", "王冠还没掉！想拍扁朕，先问这片地答不答应！", "王冠在往下掉……原来朕只是被起源忘掉的一滩黏液。", "咕噜……朕又散架了。你明天还会来，对吧？", "朕已经不数第几次散架了；反正你一来，朕就得重新做王。", "哈！先当果冻，再当王座；朕的御座正缺一块垫子。", "又是你？拍你比整理王座还省事。", "……又倒了。朕拍得手都酸了。"),
    VOICE(4u, "我看见了你的每一次闪避。来，让我把名字写进这一页。", "那道裂缝又在移动。你的记录还没有结束。", "第三次抬头了；你终于肯承认自己被注视。", "你来了。眼睛从不忘记看过的猎物。", "裂缝正在闭合；我的第二只眼已经睁开。", "眼睛要熄了……可我终于看见了你未写完的名字。", "又闭了一次。只要你再抬头，我便还会睁开。", "别把胜利当成黑暗；我只是暂时合上眼。", "看见了。数据已登记：裂缝已闭合。", "又是你这道裂缝；你以为我记不住杀过的人吗？", "第三次了。你连逃走都不会。"),
    VOICE(13u, "饥饿从地底醒来。你听见的不是风，是牙齿。", "地层又裂开了；这一次，我会把你的路全吞下去。", "你还敢站在裂缝上？很好，饥饿认识你的重量。", "又一次进食。别让我的每一节脊骨都等太久。", "别把我逼进地底；那里才是我真正完整的身体。", "饥饿停下了……石头终于不用再替我咀嚼。", "又被切断了。但地下从不缺能重新连起的黑暗。", "你杀不死饥饿，只是让它换一条更深的路。", "地层合上了；你会成为最后一粒被嚼碎的砂。", "又回到裂缝边？你的勇气闻起来像恐惧。", "第三次坠下去吧；地下还记得你的名字。"),
    VOICE(266u, "猩红在跳。它听见你的心，也要替你跳一次。", "你又走进了脉搏中央；别怕，所有心都会归于同一节奏。", "第三次献上心跳了。猩红从不浪费熟悉的血。", "血肉的记忆醒着；它比你更早知道你会来。", "听见了吗？那不是鼓点，是你正在失去的节拍。", "心跳停了……可这团血肉仍记得你的温度。", "又一次静默。猩红会在下一次搏动时重塑我。", "你赢得太多次，反而更像被血肉豢养的习惯。", "你的心跳断拍了；猩红替你收下余温。", "又回来送血？这份执着令人感动。", "第三次了；连你的恐惧都已经很熟。"),
    VOICE(222u, "蜂巢听见陌生脚步。别碰我的蜜，也别碰我的孩子。", "你又闯进来了；花粉已经替我认出你的气味。", "第三次惊扰蜂巢。看来你真不懂离巢的距离。", "蜂群振翅；这不是欢迎，是最后的警告。", "蜂王的翅还在响。现在，整个巢穴都会追你。", "蜂鸣散了……巢穴会记住是谁让它安静。", "又一次坠落。下一批幼蜂会替我学会你的步伐。", "你把蜂巢变成了旧伤；可花粉最会保存记忆。", "翅声停在你耳边。别动，毒针会自己找到位置。", "又回来？蜂群已经为你留好了空隙。", "第三次败北；你终于成了蜂巢的一部分。"),
    VOICE(35u, "夜钟敲响。地牢的门，为一个不肯离开的灵魂再度开启。", "你又来赴约；骷髅王不喜欢迟到的客人。", "第三次穿过这扇门。你的名字已经刻在石阶上。", "钟声还在；只要夜未尽，我的审判便未尽。", "王冠之下没有仁慈。现在，听听骨头断裂的声音。", "钟声停了……诅咒终于肯松开一根手指。", "又一次失礼地散架。明夜，我仍会在门前等你。", "你拆掉的只是骨头，不是这座地牢的执念。", "天亮前倒下吧；地牢会替我把门锁好。", "又是这个客人。你的礼数比上次更差。", "第三次了；石阶都替你感到惋惜。"),
    VOICE(113u, "这里没有退路。肉墙之后，只有你不愿看见的自己。", "你又走到边界；这次，别指望世界替你让路。", "第三次面对我。你的勇气终于学会了发苦。", "边界从未移动，是你一次次把自己送来。", "别再向前了；你正在把整座地狱推向裂口。", "墙倒下了……但边界会在每个噩梦里重新竖起。", "又一次被穿透。原来你真的敢一直向前。", "你越过我越多次，身后就越没有回头的地方。", "别跑。这里每一颗眼睛都看见你会先倒下。", "又回到边界？这一次，肉墙记得你的味道。", "第三次败北；地狱已经替你腾出位置。"),
    VOICE(134u, "机械的长躯已接通。地面会先听见你被碾碎。", "线路重启；你的脚步仍然是最明显的杂讯。", "第三次测试开始。毁灭者会把误差压成尘土。", "警报重复响起：目标仍未清除。", "护甲只是外壳。现在，让钻头替我说话。", "信号熄灭……第一台机器也学会了停机。", "又一次故障。下次启动，我会删掉你的逃生路线。", "重复报废并不可耻；可你最好别把它当成习惯。", "锁定完成。你会比警报声更早停止。", "目标再次出现；这次不需要重新校准。", "第三次清除失败。记录已归档。"),
    VOICE(125u, "双子已睁眼。别以为一双目光能被分别躲开。", "我们又看见你了；这次，别把恐惧分给两个方向。", "第三次相遇。你仍分不清哪只眼先眨。", "两道红光交汇；你的影子没有第三个出口。", "一个看穿退路，一个烧尽退路。你选哪个？", "双眼暗下……可夜空仍替我们保留你的轮廓。", "又一次合眼。我们会在下一场夜色里同时醒来。", "你赢得越多，夜空就越懂得怎样盯住你。", "两双眼睛看着你倒下；别浪费最后一次眨眼。", "又回来？我们早就替你留着视线。", "第三次败北；你的影子终于不再挣扎。"),
    VOICE(127u, "齿轮开始咬合。机械王的舞步，不给迟疑留下位置。", "舞会重开；你的骨头还记得上次的节拍吗？", "第三次入场。很好，观众终于学会了站在正中央。", "齿轮没有谢幕，只有下一轮更快的旋转。", "听见链锤了吗？那是你的时间正在被卷走。", "齿轮散落……这场舞会终于有了片刻安静。", "又一次失去节拍。下次，我会把舞台转得更快。", "你拆过我太多次；但机器不会因此忘记节奏。", "舞步到此为止。你会比音乐更早停下。", "又来听谢幕曲？这次由你来倒下。", "第三次败北；齿轮替你记住了最后一步。"),
    VOICE(262u, "花开在封闭的地方。你确定要靠近那片不会凋谢的绿？", "根系又感到震动；你总爱踩进不属于自己的花园。", "第三次拨开藤蔓。它们已经学会缠住你的名字。", "花瓣仍在呼吸；你只是把自己送得更深。", "花园不再温柔。现在，每一根刺都会替我回答。", "花合上了……地下少了一次漫长的呼吸。", "又一次凋零。根会记住你经过的方向。", "你剪掉花，却把花园留在了每一条地底裂缝里。", "别挣扎。藤蔓最喜欢记住反复挣扎的人。", "又回到花园？这次根会更早认出你。", "第三次败北；花瓣会替你盖好土。"),
    VOICE(245u, "石室的门开了。远古的心脏，不欢迎后来者。", "你又吵醒石头；它们比你更有耐心。", "第三次触碰祭坛。古老不是遗忘，只是等待。", "机关仍在转。你的每一步都被石壁记下。", "太阳之火落进石室；现在，没有影子能替你躲藏。", "石心沉寂……古老的火终于回到无声。", "又一次碎裂。石头会花更久时间记恨你。", "你能砸开石巨人，却砸不开这座神庙的年月。", "石门会在你身后闭合；别指望时间替你开门。", "又来惊扰遗迹？石头已经准备好回答。", "第三次败北；神庙会把你的脚步埋进尘埃。"),
    VOICE(370u, "海面翻了。你若还想呼吸，就别把鱼钩抛向我的王国。", "潮水又送你来；海从不保证会把同一个人送回去。", "第三次逐浪。风暴已经认得你的船影。", "海没有记性？错，它只是把记忆藏得很深。", "浪头抬高了。现在，连天空也会替海压向你。", "潮汐退去……海面暂时装作什么都没发生。", "又一次沉入浪底。下回，风暴会更早等你。", "你赢过海几次，海就会为你准备几次更深的水。", "别挣扎，海会把每一次呼吸都算得很清楚。", "又被潮水带回来？真是固执的陆地人。", "第三次败北；海已经学会你的沉没方式。"),
    VOICE(439u, "仪式未完。闯入者，你正好可以成为最后一页注脚。", "你又来了；教团的火焰仍记得你的影子。", "第三次来袭。看来你比预言更难删去。", "法阵重启；这次没有人会替你读错咒文。", "面具之后没有犹豫。现在，看看谁才配解释月亮。", "仪式中断……可预言从不因一次失败作废。", "又一次被打断。下次，幻象会先替我迎接你。", "你翻过太多页，终会读到自己被写下的那一行。", "跪下吧；你的结局已经被法阵提前念出。", "又一次来袭？教团正缺一份熟悉的祭品。", "第三次败北；预言终于把你读完。"),
    VOICE(398u, "你终于走到世界的尽头。让我看看你配不配拥有明天。", "尽头仍在这里；这一次，别把侥幸叫作希望。", "第三次仰望月亮。终焉已经记住你的坐标。", "星海沉默，只等你再走近一步。", "月亮正在裂开。你以为在摧毁我，其实是在替终焉开门。", "星海熄灭了……可起源之外，还有东西正在醒来。", "又一次沉入黑暗。只要你再抬头，终焉便会回应。", "别数胜利。宇宙比你更擅长把一切带回原点。", "你的明天到此为止；月光会替我收走余温。", "又一次来到尽头？终焉从不介意重复。", "第三次败北；星海已不再为你点灯。"),
    VOICE(657u, "王座从甜蜜中升起。来吧，让舞会决定谁配戴上光。", "你又赴宴；别让糖霜掩住你发抖的手。", "第三次起舞。我的王国很少有这么固执的客人。", "皇冠仍亮着；甜美的东西，也会咬人。", "别只看见光。王座背面，藏着所有没能谢幕的人。", "王冠失色……甜蜜的梦终于肯醒一次。", "又一次落幕。下次舞会，我会换更锋利的乐章。", "你赢得越多，梦境就越舍不得放你离开。", "舞步错了。现在，请把最后一支舞跳完。", "又来赴宴？王座已经为你留了空位。", "第三次败北；糖霜会盖住你的名字。"),
    VOICE(636u, "白昼不属于你。别把我的花园误当成避风处。", "太阳又升得太亮；你为何还要站在光里？", "第三次在白昼挑战我。你的勇气近乎残忍。", "光从不疲倦，只有看着光的人会先闭眼。", "花开得更亮了。现在，连影子也救不了你。", "光暗了一瞬……原来白昼也会为胜者让路。", "又一次坠入暮色。下次太阳会更早找到你。", "你重复挑战光，仿佛黑夜从未教会你畏惧。", "别闭眼；闭眼的人会先被光抹去。", "又回到白昼？太阳正等着看你再次倒下。", "第三次败北；连影子也不愿替你停留。"),
    VOICE(668u, "雪原的门开了。陌生人，别惊醒一头只想回家的鹿。", "你又踏进风雪；我闻得出你带来的火。", "第三次穿过寒雾。森林已经不再把你当客人。", "鹿角上的霜还在；它比你的誓言更耐久。", "别逼我咆哮。风雪会替我把整片森林封住。", "风雪静下来了……鹿角终于不必再撞向陌生的火。", "又一次倒在雪里。春天也许会替我原谅你。", "你赢过严冬，却未必赢过森林对你的记忆。", "躺下吧；雪会很快把你的脚印埋好。", "又带着火回来？风雪早已认出你。", "第三次败北；森林会把你的名字冻进年轮。"),
    VOICE(551u, "龙焰掠过城墙。入侵者，别以为天空没有守卫。", "你又来挑战要塞；这次，龙焰会飞得更低。", "第三次越过城墙。你倒是比旗帜更顽固。", "战鼓未停；天空仍归守城者所有。", "火焰加速。现在，连翅膀的阴影都会切开地面。", "龙焰熄下……要塞终于能听见自己的风声。", "又一次坠落。下回，我会让天空没有缝隙。", "你击败过守城者，却从未真正夺走天空。", "别抬头太晚；火焰落下时没有第二次警告。", "又来攻城？龙焰已经在云里排好队。", "第三次败北；城墙会记住你没能跨过的高度。")
};

static uint32_t boss_voice_id(uint32_t type) {
    switch (type) {
    case 14u: case 15u: return 13u;
    case 36u: case 37u: case 38u: case 39u: return 35u;
    case 126u: return 125u;
    case 128u: case 129u: return 127u;
    default: return type;
    }
}

static const OR_BossVoice *boss_voice_find(uint32_t type) {
    size_t i;
    uint32_t id = boss_voice_id(type);
    for (i = 0u; i < sizeof(g_boss_voices) / sizeof(g_boss_voices[0]); ++i) {
        if (g_boss_voices[i].id == id) return &g_boss_voices[i];
    }
    return NULL;
}

static const char *boss_voice_line(uint32_t type, OR_BossDialogEvent event,
                                   uint32_t summon_count, uint32_t kill_count) {
    const OR_BossVoice *voice = boss_voice_find(type);
    if (!voice) return NULL; /* Unknown/event bosses deliberately stay silent. */
    if (event == OR_BOSS_DIALOG_SPAWN) return voice->arrival[summon_count < 4u ? summon_count : 3u];
    if (event == OR_BOSS_DIALOG_HALF) return voice->half;
    return voice->death[kill_count < 3u ? kill_count : 2u];
}

static const char *terrain_depth_name(OR_DepthTag depth) {
    static const char *const names[] = {"地表", "地下", "洞穴", "地狱"};
    return depth < OR_DEPTH_COUNT ? names[depth] : "地表";
}

static const char *terrain_biome_name(OR_BiomeTag biome) {
    static const char *const names[] = {"森林", "沙漠", "雪原", "丛林", "神圣", "腐化", "猩红"};
    return biome < OR_BIOME_COUNT ? names[biome] : "森林";
}

static const char *terrain_special_name(OR_SpecialLocationTag special) {
    static const char *const names[] = {"", "海洋", "地牢", "发光蘑菇洞", "天空"};
    return special < OR_SPECIAL_COUNT ? names[special] : "";
}

/* Player-facing area names must express the highest-priority location only.
 * A normal forest is the fallback biome, not a suffix for depth layers, so
 * never render misleading combinations such as “地狱·森林” or “洞穴·森林”. */
static void terrain_display_name(OR_TerrainSnapshot terrain,
                                 char *out, size_t out_size) {
    const char *special;
    if (!out || out_size == 0u) return;
    out[0] = '\0';
    special = terrain_special_name(terrain.special);
    if (special[0] != '\0') {
        (void)snprintf(out, out_size, "%s", special);
    } else if (terrain.depth == OR_DEPTH_UNDERWORLD ||
               terrain.biome == OR_BIOME_FOREST) {
        (void)snprintf(out, out_size, "%s", terrain_depth_name(terrain.depth));
    } else {
        (void)snprintf(out, out_size, "%s·%s", terrain_depth_name(terrain.depth),
                       terrain_biome_name(terrain.biome));
    }
}

static const char *progress_stage_name_zh(OR_ProgressStage stage) {
    static const char *const names[] = {
        "起源稳定期", "深层扰动期", "生命重构期", "起源崩解期", "终局重构期"
    };
    return stage < OR_PROGRESS_COUNT ? names[stage] : "未知阶段";
}

static const char *weather_name_zh(OR_Weather weather) {
    static const char *const names[] = {
        "晴朗", "降雨", "沙尘暴", "暴雪", "日食", "血月", "南瓜月", "霜月",
        "史莱姆雨", "大风"
    };
    return weather < OR_WEATHER_COUNT ? names[weather] : "未知天气";
}

static const char *world_rule_name_zh(uint32_t rule_id) {
    static const char *const names[] = {
        "重构潮汐", "强敌世界", "丰收契约", "暴风边境", "夜行法则",
        "深渊回响", "邪恶侵染", "玻璃炮"
    };
    return rule_id < OR_RULE_COUNT ? names[rule_id] : "未知规则";
}

static const char *world_rule_effect_zh(uint32_t rule_id) {
    static const char *const effects[] = {
        "重构体出现率提高，异化奖励略增",
        "重构体生命与接触伤害提高",
        "异化体额外奖励与品质提高",
        "有天气时重构体出现率提高",
        "夜晚重构体移动节奏提高",
        "地下洞穴更易出现灾变体",
        "腐化、猩红区域偏向对应属性",
        "生命降低，但接触伤害提高"
    };
    return rule_id < OR_RULE_COUNT ? effects[rule_id] : "效果未知";
}

static const char *terrain_environment_effect_zh(OR_TerrainSnapshot terrain) {
    if (terrain.special == OR_SPECIAL_OCEAN) return "海洋：水系攻击倾向提高";
    if (terrain.special == OR_SPECIAL_DUNGEON) return "地牢：特殊攻击倾向提高";
    if (terrain.special == OR_SPECIAL_MUSHROOM) return "蘑菇地：特殊能力倾向提高";
    if (terrain.special == OR_SPECIAL_SKY) return "天空：飞行单位更活跃";
    switch (terrain.depth) {
    case OR_DEPTH_UNDERWORLD: return "地狱：火属性攻击倾向提高";
    case OR_DEPTH_CAVERN: return "洞穴：生命、防御提高；灾变体略多";
    case OR_DEPTH_UNDERGROUND: return "地下：生命、防御略有提高";
    default: break;
    }
    switch (terrain.biome) {
    case OR_BIOME_DESERT: return "沙漠：伤害提高，灾变体略多";
    case OR_BIOME_SNOW: return "雪原：防御提高，偏向冰属性";
    case OR_BIOME_JUNGLE: return "丛林：生命、伤害提高，偏向毒属性";
    case OR_BIOME_HALLOW: return "神圣：生命、防御提高，灾变体略多";
    case OR_BIOME_CORRUPTION: return "腐化：生命、伤害提高，偏向腐化属性";
    case OR_BIOME_CRIMSON: return "猩红：生命、伤害、防御提高";
    default: return "森林：异化体出现倾向提高";
    }
}

static const char *tier_prefix(OR_EliteTier tier) {
    switch (tier) {
    case OR_TIER_APOCALYPSE: return "终焉体";
    case OR_TIER_CALAMITY: return "灾变体";
    case OR_TIER_ALTERED: return "异化体";
    default: return NULL;
    }
}

static const char *boss_name_zh(uint32_t npc_type) {
    switch (npc_type) {
    case 50u: return "史莱姆王"; case 4u: return "克苏鲁之眼";
    case 13u: case 14u: case 15u: return "世界吞噬者";
    case 266u: return "克苏鲁之脑"; case 222u: return "蜂王";
    case 35u: return "骷髅王"; case 36u: case 37u: case 38u: case 39u: return "骷髅王";
    case 113u: return "血肉墙"; case 134u: return "毁灭者";
    case 125u: case 126u: return "双子魔眼";
    case 127u: case 128u: case 129u: return "机械骷髅王";
    case 262u: return "世纪之花"; case 245u: return "石巨人";
    case 370u: return "猪龙鱼公爵"; case 439u: return "拜月教邪教徒";
    case 398u: return "月亮领主"; case 657u: return "史莱姆女皇";
    case 636u: return "光之女皇"; case 668u: return "鹿角怪";
    case 551u: return "贝特西"; default: return NULL;
    }
}

static void tier_rgba(OR_EliteTier tier, uint8_t rgba[4]) {
    uint32_t packed;
    switch (tier) {
    case OR_TIER_ALTERED: packed = 0xFF8FFFA8u; break; /* mint */
    case OR_TIER_CALAMITY: packed = 0xFF80C7FFu; break; /* sky */
    case OR_TIER_APOCALYPSE: packed = 0xFFF0A0FFu; break; /* lilac */
    default: packed = 0xFFFFFFFFu; break;
    }
    rgba[0] = (uint8_t)((packed >> 16) & 0xFFu);
    rgba[1] = (uint8_t)((packed >> 8) & 0xFFu);
    rgba[2] = (uint8_t)(packed & 0xFFu);
    rgba[3] = (uint8_t)((packed >> 24) & 0xFFu);
}

static const char *channel_hex(const char *channel) {
    if (!channel) return "FFFFFF";
    if (strcmp(channel, "terrain") == 0) return "8AE7FF";
    if (strcmp(channel, "weather") == 0) return "FFC477";
    if (strcmp(channel, "world_rule") == 0) return "FFE08A";
    if (strcmp(channel, "boss") == 0) return "FF9CA8";
    if (strcmp(channel, "elite_altered") == 0) return "8FFFA8";
    if (strcmp(channel, "elite_calamity") == 0) return "80C7FF";
    if (strcmp(channel, "elite_apocalypse") == 0) return "F0A0FF";
    return "FFFFFF";
}

static const char *channel_body_hex(const char *channel) {
    if (!channel) return "FFFFFF";
    if (strcmp(channel, "terrain") == 0) return "C8F5FF";
    if (strcmp(channel, "weather") == 0) return "FFE7BF";
    if (strcmp(channel, "world_rule") == 0) return "FFF2B8";
    if (strcmp(channel, "boss") == 0) return "FFD4DA";
    if (strcmp(channel, "elite_altered") == 0) return "D9FFD9";
    if (strcmp(channel, "elite_calamity") == 0) return "D8EEFF";
    if (strcmp(channel, "elite_apocalypse") == 0) return "F0DBFF";
    return "FFFFFF";
}

static const char *elite_channel(OR_EliteTier tier) {
    switch (tier) {
    case OR_TIER_ALTERED: return "elite_altered";
    case OR_TIER_CALAMITY: return "elite_calamity";
    case OR_TIER_APOCALYPSE: return "elite_apocalypse";
    default: return "elite";
    }
}

static bool wrap_chat_color(char *message, size_t message_size,
                            const char *hex) {
    char body[256];
    if (!message || !message_size || !hex) return false;
    (void)snprintf(body, sizeof(body), "%s", message);
    return snprintf(message, message_size, "[c/%s:%s]", hex, body) <
           (int)message_size;
}

void or_broadcast_init(OR_BroadcastState *state) {
    if (state) {
        memset(state, 0, sizeof(*state));
        state->next_message_id = 1u;
    }
}

void or_broadcast_on_player_respawn(OR_BroadcastState *state) {
    if (!state) return;
    /* A respawn is not a new world: keep Boss narrative memory, but allow
     * bootstrap notices and the next private defeat event to be displayed. */
    state->last_emit_tick = 0u;
    /* Do not clear the Boss lock here.  Player identity is recreated during
     * respawn, but the defeat dialogue was just emitted on the previous tick
     * and must remain exclusive for its full display window. */
    state->last_terrain_key = 0u;
    state->last_terrain_tick = 0u;
    state->last_world_key = 0u;
    state->last_world_tick = 0u;
    state->last_daily_broadcast_wall_second = 0u;
    /* The world-state card and the rule-summary card form one entry
     * sequence.  Re-entry creates a new LocalPlayer object on this Android
     * build, so retaining the old summary key would let card 1 through but
     * silently suppress card 2 as a duplicate.  Rule data itself is not
     * changed here; only this player-facing replay guard is cleared. */
    state->last_rule_summary_key = 0u;
}

static uint32_t next_message_id(const OR_BroadcastState *state) {
    return state && state->next_message_id != 0u ? state->next_message_id : 1u;
}

static void commit_message_id(OR_BroadcastState *state, uint32_t message_id) {
    uint32_t next;
    if (!state) return;
    state->last_message_id = message_id;
    next = message_id + 1u;
    state->next_message_id = next == 0u ? 1u : next;
}

static bool invoke_new_text(const OR_Runtime *runtime,
                            patch_handle_t text_handle,
                            const uint8_t rgba[4],
                            bool force_display,
    uint64_t *ignored_return) {
    void *args[4] = {NULL, NULL, NULL, NULL};
    if (!runtime || !text_handle || !rgba || !ignored_return ||
        !runtime->method_main_new_text || !patchlib_method_invoke_args) return false;
    args[0] = &text_handle;
    if (runtime->main_new_text_arg_count == 1) {
        return patchlib_method_invoke_args(runtime->method_main_new_text,
                                           PATCH_NULL, ignored_return, args);
    }
    if (runtime->main_new_text_arg_count == 3 &&
        runtime->main_new_text_color_type == PATCH_POINTER) {
#if defined(__ANDROID__)
        uint8_t native_rgba[4] = {rgba[0], rgba[2], rgba[1], rgba[3]};
        uint64_t color_slot = 0u;
        static bool color_abi_logged;
        /*
         * The mobile game exposes Color as a pointer-valued ABI slot.  The
         * working ThreeDayEdict path passes the four bytes through an aligned
         * 64-bit slot and invokes the ordinary argument bridge.  The generic
         * value bridge is not available in every production kernel and made
         * this otherwise valid NewText signature fail at runtime.
         */
        memcpy(&color_slot, native_rgba, sizeof(native_rgba));
        args[1] = &color_slot;
        args[2] = &force_display;
        if (!color_abi_logged) {
            color_abi_logged = true;
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[NEWTEXT_COLOR_ABI] mode=arg_slot bytes=red,blue,green,alpha "
                   "size=8 verified=yes");
        }
        return patchlib_method_invoke_args(runtime->method_main_new_text,
                                           PATCH_NULL, ignored_return, args);
#else
        (void)force_display;
        return false;
#endif
    }
    if (runtime->main_new_text_arg_count == 4) {
        uint8_t red = rgba[0];
        uint8_t green = rgba[1];
        uint8_t blue = rgba[2];
        int32_t red_i = (int32_t)rgba[0];
        int32_t green_i = (int32_t)rgba[1];
        int32_t blue_i = (int32_t)rgba[2];
        if (runtime->main_new_text_color_type == PATCH_UINT8) {
            args[1] = &red;
            args[2] = &green;
            args[3] = &blue;
        } else if (runtime->main_new_text_color_type == PATCH_INT32) {
            args[1] = &red_i;
            args[2] = &green_i;
            args[3] = &blue_i;
        } else {
            return false;
        }
        return patchlib_method_invoke_args(runtime->method_main_new_text,
                                           PATCH_NULL, ignored_return, args);
    }
    return false;
}

/*
 * OriginRewrite keeps Terraria's native text markup in the string itself.
 * ThreeDayEdict's card layout is reproduced by sending one short, colored
 * NewText line at a time.  This is deliberately centralized so every
 * broadcast channel keeps the same [c/HEX:text] format and ABI handling.
 */
static bool emit_card_line(const OR_Runtime *runtime,
                           const char *channel,
                           const char *message,
                           const uint8_t rgba[4],
                           bool emphasis) {
    char formatted[320];
    patch_handle_t text;
    uint64_t ignored_return = 0u;
    const char *hex;
    int written;

    if (!runtime || !channel || !message || !rgba) return false;
    hex = emphasis ? channel_hex(channel) : channel_body_hex(channel);
    /* wrap_chat_color wraps the plain card line in OriginRewrite's markup. */
    written = snprintf(formatted, sizeof(formatted), "%s", message);
    if (written < 0 || written >= (int)sizeof(formatted)) return false;
    if (!wrap_chat_color(formatted, sizeof(formatted), hex)) return false;
    text = patchlib_string_create ? patchlib_string_create(formatted) : PATCH_NULL;
    if (!text || !patchlib_method_invoke_args) return false;
    if (!invoke_new_text(runtime, text, rgba, true, &ignored_return)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[BROADCAST_LINE_SAFE_OFF] channel=%s reason=invoke_failed",
               channel);
        return false;
    }
    return true;
}

bool or_broadcast_emit_elite(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_EliteTier tier,
                             uint32_t npc_type,
                             uint64_t generation_id,
                             uint64_t snapshot_revision,
                             uint64_t now_tick) {
    char message[256];
    uint32_t message_id;
    uint8_t rgba[4];
    const char *prefix;
    const char *channel;
    const char *event_title;
    bool card_ok;

    if (!state || !runtime || tier < OR_TIER_ALTERED || tier > OR_TIER_APOCALYPSE) return false;
    if (now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BOSS_DIALOG_LOCK] channel=elite action=blocked remaining=%llu",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    message_id = next_message_id(state);
    prefix = tier_prefix(tier);
    tier_rgba(tier, rgba);
    channel = elite_channel(tier);
    event_title = tier == OR_TIER_ALTERED
                      ? "╰─ 起源律动 · 异化降临 ─╯"
                      : (tier == OR_TIER_CALAMITY
                             ? "╰─ 起源律动 · 灾变降临 ─╯"
                             : "╰─ 起源律动 · 终焉降临 ─╯");
    if (!prefix || !runtime->capabilities.new_text_ready ||
        (runtime->main_new_text_arg_count != 1 &&
         runtime->main_new_text_arg_count != 3 &&
         runtime->main_new_text_arg_count != 4) ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=api_unavailable",
               (unsigned)message_id, (unsigned)npc_type);
        return false;
    }
    if (generation_id != 0u && state->last_generation_id == generation_id &&
        state->last_elite_tier == (uint32_t)tier) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_DEDUP] messageId=%u generation=%llu",
               (unsigned)message_id, (unsigned long long)generation_id);
        return false;
    }
    if (state->last_emit_tick != 0u && now_tick < state->last_emit_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_COOLDOWN] messageId=%u generation=%llu",
               (unsigned)message_id, (unsigned long long)generation_id);
        return false;
    }
    /* A compact four-line card keeps the alert readable on narrow screens. */
    card_ok = emit_card_line(runtime, channel,
                             tier == OR_TIER_ALTERED
                                 ? "╭─ 起源律动 · 异化体警报 ─╮"
                                 : (tier == OR_TIER_CALAMITY
                                        ? "╭─ 起源律动 · 灾变体警报 ─╮"
                                        : "╭─ 起源律动 · 终焉体警报 ─╮"),
                             rgba, true);
    (void)snprintf(message, sizeof(message), "│ 等级：%s", prefix);
    card_ok = emit_card_line(runtime, channel, message, rgba, false) && card_ok;
    (void)snprintf(message, sizeof(message), "│ 世界规则发生偏移，%s已现身。", prefix);
    card_ok = emit_card_line(runtime, channel, message, rgba, false) && card_ok;
    card_ok = emit_card_line(runtime, channel,
                             event_title, rgba, true) && card_ok;
    if (!card_ok) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=card_incomplete",
               (unsigned)message_id, (unsigned)npc_type);
        return false;
    }
    commit_message_id(state, message_id);
    state->last_elite_tier = (uint32_t)tier;
    state->last_generation_id = generation_id;
    state->last_emit_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COMMIT] messageId=%u channel=elite_spawn scope=local_host "
           "type=%u generation=%llu snapshotRevision=%llu",
           (unsigned)message_id, (unsigned)npc_type,
           (unsigned long long)generation_id,
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_terrain(OR_BroadcastState *state,
                               const OR_Runtime *runtime,
                               OR_TerrainSnapshot terrain,
                               uint64_t snapshot_revision,
                               uint64_t now_tick) {
    char message[256];
    uint8_t rgba[4] = {145u, 255u, 176u, 255u}; /* environment: fresh green */
    uint32_t key = ((uint32_t)terrain.depth << 16) |
                   ((uint32_t)terrain.biome << 8) | (uint32_t)terrain.special;
    const char *special = terrain_special_name(terrain.special);
    char display_name[96];
    bool card_ok;
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args || terrain.depth >= OR_DEPTH_COUNT ||
        terrain.biome >= OR_BIOME_COUNT || terrain.special >= OR_SPECIAL_COUNT) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=api_or_snapshot_unavailable");
        return false;
    if (now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BOSS_DIALOG_LOCK] channel=terrain action=blocked remaining=%llu",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    }
    if (state->last_terrain_key == key && state->last_terrain_tick != 0u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=duplicate key=%u", (unsigned)key);
        return false;
    }
    if (state->last_terrain_tick != 0u && now_tick < state->last_terrain_tick + 600u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    card_ok = emit_card_line(runtime, "terrain",
                             "╭─ 起源律动 · 环境律动 ─╮", rgba, true);
    terrain_display_name(terrain, display_name, sizeof(display_name));
    (void)snprintf(message, sizeof(message), "│ 区域：%s", display_name);
    card_ok = emit_card_line(runtime, "terrain", message, rgba, false) && card_ok;
    (void)snprintf(message, sizeof(message), "│ %s", terrain_environment_effect_zh(terrain));
    card_ok = emit_card_line(runtime, "terrain", message, rgba, false) && card_ok;
    card_ok = emit_card_line(runtime, "terrain",
                             "╰─ 起源律动 · 环境已生效 ─╯", rgba, true) && card_ok;
    if (!card_ok) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=card_incomplete key=%u", (unsigned)key);
        return false;
    }
    state->last_terrain_key = key;
    state->last_terrain_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[TERRAIN_BROADCAST_COMMIT] depth=%s biome=%s special=%s revision=%llu",
           terrain_depth_name(terrain.depth), terrain_biome_name(terrain.biome),
           special[0] != '\0' ? special : "none",
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_world(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_Weather weather,
                             bool is_night,
                             uint64_t snapshot_revision,
                             uint64_t now_tick) {
    char message[256];
    uint8_t rgba[4] = {255u, 196u, 119u, 255u}; /* weather: soft amber */
    uint32_t key = ((uint32_t)weather << 1) | (is_night ? 1u : 0u);
    const char *weather_name;
    bool card_ok;
    if (state && now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BOSS_DIALOG_LOCK] channel=world action=blocked remaining=%llu",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args || weather >= OR_WEATHER_COUNT) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=api_or_snapshot_unavailable");
        return false;
    }
    if (state->last_world_key == key && state->last_world_tick != 0u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=duplicate key=%u", (unsigned)key);
        return false;
    }
    if (state->last_world_tick != 0u && now_tick < state->last_world_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    weather_name = weather_name_zh(weather);
    card_ok = emit_card_line(runtime, "weather",
                             "╭─ 起源律动 · 世界状态 ─╮", rgba, true);
    (void)snprintf(message, sizeof(message), "│ 时相：%s", is_night ? "夜晚" : "白昼");
    card_ok = emit_card_line(runtime, "weather", message, rgba, false) && card_ok;
    (void)snprintf(message, sizeof(message), "│ 天象：%s", weather_name);
    card_ok = emit_card_line(runtime, "weather", message, rgba, false) && card_ok;
    card_ok = emit_card_line(runtime, "weather",
                             "╰─ 起源律动 · 当前世界 ─╯", rgba, true) && card_ok;
    if (!card_ok) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[WORLD_BROADCAST_SKIP] reason=card_incomplete key=%u", (unsigned)key);
        return false;
    }
    state->last_world_key = key;
    state->last_world_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[WORLD_BROADCAST_COMMIT] night=%s weather=%s key=%u revision=%llu",
           is_night ? "yes" : "no", weather_name, (unsigned)key,
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_rule_summary(OR_BroadcastState *state,
                                    const OR_Runtime *runtime,
                                    const OR_RuleSnapshot *snapshot,
                                    uint64_t rule_revision,
                                    uint64_t now_tick) {
    (void)now_tick;
    char message[256];
    uint8_t rgba[4] = {255u, 224u, 138u, 255u}; /* world rules: pale gold */
    size_t i;
    bool card_ok;
    if (state && now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BOSS_DIALOG_LOCK] channel=world_rule action=blocked remaining=%llu",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    if (!state || !runtime || !snapshot || !snapshot->selected_count ||
        !runtime->capabilities.new_text_ready || !runtime->method_main_new_text ||
        !patchlib_string_create || !patchlib_method_invoke_args) return false;
    {
        uint64_t key = ((uint64_t)snapshot->active_mask << 32) |
                       ((uint64_t)snapshot->progress << 24) |
                       (rule_revision & UINT64_C(0xFFFFFF));
        if (state->last_rule_summary_key == key) return false;
    }
    (void)snprintf(message, sizeof(message), "╭─ 起源律动 · 世界规则 ─╮");
    card_ok = emit_card_line(runtime, "world_rule", message, rgba, true);
    (void)snprintf(message, sizeof(message), "│ 阶段：%s",
                   progress_stage_name_zh(snapshot->progress));
    card_ok = emit_card_line(runtime, "world_rule", message, rgba, false) && card_ok;
    card_ok = emit_card_line(runtime, "world_rule",
                             "│ 本轮规则已载入：", rgba, false) && card_ok;
    for (i = 0u; i < snapshot->selected_count && i < OR_MAX_WORLD_RULES; ++i) {
        (void)snprintf(message, sizeof(message), "│ %s：%s",
                       world_rule_name_zh(snapshot->selected_ids[i]),
                       world_rule_effect_zh(snapshot->selected_ids[i]));
        card_ok = emit_card_line(runtime, "world_rule", message, rgba, false) && card_ok;
    }
    card_ok = emit_card_line(runtime, "world_rule",
                             "╰─ 起源律动 · 规则状态 ─╯", rgba, true) && card_ok;
    if (!card_ok) return false;
    state->last_rule_summary_key = ((uint64_t)snapshot->active_mask << 32) |
                                   ((uint64_t)snapshot->progress << 24) |
                                   (rule_revision & UINT64_C(0xFFFFFF));
    OR_LOG(MOD_LOG_LEVEL_INFO, "[RULE_SUMMARY_BROADCAST] stage=%s revision=%llu rules=%u",
           progress_stage_name_zh(snapshot->progress), (unsigned long long)rule_revision,
           (unsigned)snapshot->selected_count);
    return true;
}

bool or_broadcast_emit_daily(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             const OR_RuleSnapshot *snapshot,
                             uint64_t rule_revision,
                             uint64_t now_tick) {
    char message[256];
    uint8_t rgba[4] = {255u, 224u, 138u, 255u};
    size_t i;
    bool card_ok;
    uint64_t wall_second;
    if (!state || !runtime || !snapshot || snapshot->selected_count < 2u ||
        snapshot->selected_count > OR_MAX_WORLD_RULES ||
        !runtime->capabilities.new_text_ready || !runtime->method_main_new_text ||
        !patchlib_string_create || !patchlib_method_invoke_args) return false;
    (void)now_tick;
    wall_second = (uint64_t)time(NULL);
    if (state->last_daily_broadcast_wall_second != 0u &&
        wall_second < state->last_daily_broadcast_wall_second +
                          (OR_DAILY_BROADCAST_INTERVAL_TICKS / 60u)) {
        return false;
    }
    if (now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[DAILY_BROADCAST] action=deferred reason=boss_dialog_lock remaining=%llu",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    card_ok = emit_card_line(runtime, "world_rule",
                             "╭─ 起源律动 · 每日播报 ─╮", rgba, true);
    (void)snprintf(message, sizeof(message), "│ 当前世界规则：%u 条（第 %llu 轮）",
                   (unsigned)snapshot->selected_count,
                   (unsigned long long)rule_revision);
    card_ok = emit_card_line(runtime, "world_rule", message, rgba, false) && card_ok;
    for (i = 0u; i < snapshot->selected_count && i < OR_MAX_WORLD_RULES; ++i) {
        (void)snprintf(message, sizeof(message), "│ %s：%s",
                       world_rule_name_zh(snapshot->selected_ids[i]),
                       world_rule_effect_zh(snapshot->selected_ids[i]));
        card_ok = emit_card_line(runtime, "world_rule", message, rgba, false) && card_ok;
    }
    card_ok = emit_card_line(runtime, "world_rule",
                             "╰─ 起源律动 · 今日规则有效 ─╯", rgba, true) && card_ok;
    if (!card_ok) return false;
    state->last_daily_broadcast_wall_second = wall_second;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[DAILY_BROADCAST] shown=yes intervalTicks=%u intervalSeconds=%u revision=%llu rules=%u",
           (unsigned)OR_DAILY_BROADCAST_INTERVAL_TICKS,
           (unsigned)(OR_DAILY_BROADCAST_INTERVAL_TICKS / 60u),
           (unsigned long long)rule_revision,
           (unsigned)snapshot->selected_count);
    return true;
}

bool or_broadcast_emit_boss_dialog(OR_BroadcastState *state, const OR_Runtime *runtime,
                                   uint32_t npc_type, OR_BossDialogEvent event, uint64_t now_tick) {
    char message[256];
    uint8_t rgba[4]={255u,156u,168u,255u};
    bool card_ok;
    const char *boss_name;
    uint32_t voice_id;
    uint32_t summon_count = 0u;
    uint32_t kill_count = 0u;
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        (runtime->main_new_text_arg_count != 1 && runtime->main_new_text_arg_count != 3 &&
         runtime->main_new_text_arg_count != 4) || !patchlib_string_create || !patchlib_method_invoke_args) return false;
    if (event != OR_BOSS_DIALOG_SPAWN && event != OR_BOSS_DIALOG_HALF && event != OR_BOSS_DIALOG_DEATH) return false;
    /* Spawn/half events wait for the previous card.  Death is terminal and is
     * allowed to take priority so a fast kill cannot lose its conclusion. */
    if (event != OR_BOSS_DIALOG_DEATH && now_tick < state->boss_lock_until_tick) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BOSS_DIALOG_LOCK] channel=boss event=%s action=deferred remaining=%llu",
               event == OR_BOSS_DIALOG_HALF ? "half" : "spawn",
               (unsigned long long)(state->boss_lock_until_tick - now_tick));
        return false;
    }
    voice_id = boss_voice_id(npc_type);
    if (voice_id >= 1024u || !boss_voice_find(voice_id)) return false;
    boss_name = boss_name_zh(npc_type);
    if (event == OR_BOSS_DIALOG_SPAWN) {
        summon_count = state->boss_summon_count[voice_id]++;
    } else {
        summon_count = state->boss_summon_count[voice_id];
    }
    if (event == OR_BOSS_DIALOG_DEATH) {
        kill_count = state->boss_kill_count[voice_id]++;
    } else {
        kill_count = state->boss_kill_count[voice_id];
    }
    card_ok = emit_card_line(runtime, "boss",
                             event == OR_BOSS_DIALOG_HALF
                                 ? "╭─ Boss战 · 防线瓦解 ─╮"
                                 : (event == OR_BOSS_DIALOG_DEATH
                                        ? "╭─ Boss战 · 战斗结算 ─╮"
                                        : "╭─ Boss战 · 领域开启 ─╮"),
                             rgba, true);
    (void)snprintf(message, sizeof(message), "│ %s",
                   boss_voice_line(voice_id, event, summon_count, kill_count));
    card_ok = emit_card_line(runtime, "boss", message, rgba, false) && card_ok;
    card_ok = emit_card_line(runtime, "boss",
                             "╰─ 起源律动 · 首领事件 ─╯", rgba, true) && card_ok;
    if (!card_ok) return false;
    state->last_emit_tick=now_tick;
    state->boss_lock_until_tick = now_tick + OR_BOSS_DIALOG_LOCK_TICKS;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BOSS_DIALOG] type=%u voiceId=%u event=%s summon=%u killsBefore=%u lockUntil=%llu",
           (unsigned)npc_type, (unsigned)voice_id,
           event == OR_BOSS_DIALOG_HALF ? "half" :
               (event == OR_BOSS_DIALOG_DEATH ? "death" : "spawn"),
           (unsigned)summon_count, (unsigned)kill_count,
           (unsigned long long)state->boss_lock_until_tick);
    (void)boss_name; /* Retained as the resolved display identity for diagnostics/UI extension. */
    return true;
}

bool or_broadcast_emit_boss_player_death(OR_BroadcastState *state,
                                         const OR_Runtime *runtime,
                                         uint32_t npc_type, uint32_t death_count,
                                         uint64_t now_tick) {
    char message[256];
    uint8_t rgba[4] = {255u, 116u, 128u, 255u};
    const char *line;
    const OR_BossVoice *voice;
    uint32_t voice_id;
    bool ok;
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        !patchlib_string_create || !patchlib_method_invoke_args) return false;
    voice_id = boss_voice_id(npc_type);
    if (voice_id >= 1024u || !(voice = boss_voice_find(voice_id))) return false;
    (void)death_count; /* Adapter supplies a diagnostic count; state is authoritative. */
    death_count = ++state->boss_player_death_count[voice_id];
    line = voice->defeat[death_count <= 1u ? 0u : (death_count == 2u ? 1u : 2u)];
    ok = emit_card_line(runtime, "boss", "╭─ Boss战 · 败北回响 ─╮", rgba, true);
    (void)snprintf(message, sizeof(message), "│ %s", line);
    ok = emit_card_line(runtime, "boss", message, rgba, false) && ok;
    ok = emit_card_line(runtime, "boss", "╰─ Boss战 · 领域仍在 ─╯", rgba, true) && ok;
    if (!ok) return false;
    state->last_emit_tick = now_tick;
    state->boss_lock_until_tick = now_tick + OR_BOSS_DIALOG_LOCK_TICKS;
    OR_LOG(MOD_LOG_LEVEL_INFO, "[BOSS_PLAYER_DEATH] type=%u voiceId=%u count=%u lockUntil=%llu",
           (unsigned)npc_type, (unsigned)voice_id, (unsigned)death_count,
           (unsigned long long)state->boss_lock_until_tick);
    return true;
}

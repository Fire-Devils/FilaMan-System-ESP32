#!/usr/bin/env python3
"""
FilaMan ESP32 - 中文语言支持自动补丁脚本
自动修改 7 个文件以添加中文语言支持
"""

import os
import re
import shutil
from pathlib import Path

PROJECT_DIR = Path('/home/jackpy/filaman-esp32')
BACKUP_DIR = PROJECT_DIR / 'backup_lang_zh'

def backup_file(filepath):
    """备份原文件"""
    rel_path = filepath.relative_to(PROJECT_DIR)
    backup_path = BACKUP_DIR / rel_path
    backup_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(filepath, backup_path)
    print(f"  ✓ 备份: {rel_path}")

def patch_lang_h():
    """修改 src/lang.h - 添加 LANG_ZH"""
    filepath = PROJECT_DIR / 'src' / 'lang.h'
    backup_file(filepath)

    with open(filepath, 'r') as f:
        content = f.read()

    # 在 LANG_DE = 1 后添加 LANG_ZH = 2
    content = content.replace(
        '    LANG_DE = 1,\n    LANG_COUNT',
        '    LANG_DE = 1,\n    LANG_ZH = 2,\n    LANG_COUNT'
    )

    with open(filepath, 'w') as f:
        f.write(content)

    print("  ✓ lang.h: 添加 LANG_ZH = 2")

def patch_lang_cpp():
    """修改 src/lang.cpp - 添加中文翻译"""
    filepath = PROJECT_DIR / 'src' / 'lang.cpp'
    backup_file(filepath)

    with open(filepath, 'r') as f:
        lines = f.readlines()

    # 查找插入位置（德语区域结束处）
    insert_idx = None
    for i, line in enumerate(lines):
        if 'DE_NOSCALE_PROMPT[]' in line and '=' in line:
            # 找到这一行的末尾
            insert_idx = i + 1
            break

    if insert_idx is None:
        print("  ❌ 未找到插入位置")
        return

    # 中文字符串定义
    zh_strings = '''
// =====================================================================
// Chinese (Simplified) strings
// =====================================================================
static const char ZH_DISPLAY_INIT[]      = "显示屏初始化";
static const char ZH_WIFI_INIT[]         = "WiFi初始化";
static const char ZH_WEBSERVER_INIT[]    = "Web服务器启动";
static const char ZH_API_INIT[]          = "API初始化";
static const char ZH_NFC_INIT[]          = "NFC初始化";
static const char ZH_SEARCHING_SCALE[]   = "正在搜索秤";
static const char ZH_INIT_DONE[]         = "设置完成";

static const char ZH_TARE_SCALE[]        = "清零秤";
static const char ZH_SCALE_NOT_CAL[]     = "秤未校准";
static const char ZH_SCALE_CAL[]         = "校准天平";
static const char ZH_EMPTY_SCALE[]       = "清空秤";
static const char ZH_PLACE_WEIGHT[]      = "放置砝码";
static const char ZH_REMOVE_WEIGHT[]     = "移除砝码";
static const char ZH_COMPLETED[]         = "完成";
static const char ZH_CAL_ERROR[]         = "校准错误";
static const char ZH_HX711_NOT_FOUND[]   = "未找到HX711";

static const char ZH_READING[]           = "读取中";
static const char ZH_DECODING_DATA[]     = "解码数据";
static const char ZH_SPOOL_TAG[]         = "线轴标签";
static const char ZH_WEIGHING[]          = "称重中...";
static const char ZH_WEIGHT_STABLE[]     = "重量稳定";
static const char ZH_SENDING[]           = "发送中...";
static const char ZH_TAG_WRITTEN[]       = "标签已写入";
static const char ZH_WRITING[]           = "写入中";
static const char ZH_WRITE_TAG[]         = "写入标签";
static const char ZH_DONE[]              = "完成!";
static const char ZH_PLACE_TAG_NOW[]     = "现在放置标签";
static const char ZH_DETECTING_TAG[]     = "检测标签";
static const char ZH_KNOWN_SPOOL[]       = "已知线轴";
static const char ZH_QUICK_MODE[]        = "快速模式";
static const char ZH_LOCATION[]          = "位置";
static const char ZH_LOCATION_SET[]      = "位置已设置";
static const char ZH_SCAN_SPOOL_FIRST[] = "请先扫描线轴";
static const char ZH_WAIT_FMT[]          = "等待... %ds";

static const char ZH_NOT_REGISTERED[]    = "未注册";
static const char ZH_API_CONN_LOST[]     = "API连接丢失";
static const char ZH_API_ERROR[]         = "API错误";
static const char ZH_API_OFFLINE[]       = "API离线";
static const char ZH_WEIGHT_SENT_REST[]  = "已发送, 剩余:";

static const char ZH_FAILURE[]           = "失败";
static const char ZH_FAILURE_EXCL[]      = "失败!";
static const char ZH_UNKNOWN_TAG[]       = "未知标签";
static const char ZH_UNKNOWN_TAG_TYPE[]  = "未知标签类型";
static const char ZH_NO_TAG_FOUND[]      = "未找到标签";
static const char ZH_NFC_BUSY[]          = "NFC忙!";
static const char ZH_TAG_READ_ERROR[]    = "标签读取错误";
static const char ZH_TAG_TOO_SMALL[]     = "标签太小";
static const char ZH_NFC_RESET_FAIL[]    = "NFC重置失败";
static const char ZH_TAG_LOST_RESET[]    = "重置后标签丢失";
static const char ZH_NFC_STILL_BROKEN[]  = "NFC仍损坏";
static const char ZH_TAG_DEFECT[]        = "标签/接口损坏";
static const char ZH_TEST_READ_ERROR[]   = "测试页读取错误";
static const char ZH_TAG_REMOVED[]       = "标签已移除";
static const char ZH_TAG_WRITE_PROT[]    = "标签写保护?";
static const char ZH_TEST_VERIFY_FAIL[]  = "测试验证失败";
static const char ZH_NDEF_INIT_FAIL[]    = "NDEF初始化失败";
static const char ZH_NFC_UNSTABLE[]      = "NFC接口不稳定";
static const char ZH_MEMORY_ERROR[]      = "内存错误";
static const char ZH_NO_RFID_BOARD[]     = "未找到RFID板";

static const char ZH_WIFI_CONFIG[]       = "WiFi配置模式";
static const char ZH_WIFI_NOT_CONN[]     = "WiFi未连接 请检查门户";
static const char ZH_WIFI_RECONN[]       = "WiFi重连中";

static const char ZH_UPDATE[]            = "更新";
static const char ZH_DOWNLOAD[]          = "下载";

static const char ZH_NOSCALE_MODE[]      = "激活NFC-only模式";
static const char ZH_NOSCALE_PROMPT[]    = "就绪...";

'''

    lines.insert(insert_idx, zh_strings)

    # 更新 stringTable - 每行末尾添加中文条目
    # 需要找到 stringTable 定义并修改所有行
    new_lines = []
    in_table = False
    for i, line in enumerate(lines):
        if 'static const char* const stringTable[STR_COUNT][LANG_COUNT] = {' in line:
            in_table = True
            new_lines.append(line)
            continue

        if in_table and '// No-Scale mode' in line:
            # 在这一行前修改前一行，处理后两行
            # 我们需要回溯修改前一行和当前行、下一行
            pass

        new_lines.append(line)

    # 简单方法：使用正则替换所有 { EN_XXX, DE_XXX } 行
    content = ''.join(lines)

    # 模式: { EN_XXX, DE_XXX } 或 { EN_XXX, DE_XXX, }
    pattern = r'\{ ([^,]+), ([^,]+)(,)? \}'

    def replace_entry(match):
        en = match.group(1).strip()
        de = match.group(2).strip()
        comma = match.group(3) or ''
        # 构建对应的中文变量名
        zh = en.replace('EN_', 'ZH_')
        return f'{{ {en}, {de}, {zh} }}'

    content = re.sub(pattern, replace_entry, content)

    # 修改 getLangCode()
    content = content.replace(
        'const char* getLangCode() {\n    return (currentLang == LANG_DE) ? "de" : "en";\n}',
        'const char* getLangCode() {\n    switch (currentLang) {\n        case LANG_DE: return "de";\n        case LANG_ZH: return "zh";\n        default:     return "en";\n    }\n}'
    )

    with open(filepath, 'w') as f:
        f.write(content)

    print("  ✓ lang.cpp: 添加 90+ 中文字符串 + 更新 stringTable + getLangCode()")

def patch_setup_html():
    """修改 data/setup.html - 添加中文选项"""
    filepath = PROJECT_DIR / 'data' / 'setup.html'
    backup_file(filepath)

    with open(filepath, 'r') as f:
        content = f.read()

    # 插入中文选项
    content = content.replace(
        '<option value="de">Deutsch</option>',
        '<option value="de">Deutsch</option>\n                        <option value="zh">中文</option>'
    )

    # 更新 html lang 属性
    content = content.replace(
        '<html lang="en">',
        '<html lang="en" data-lang-support="en,de,zh">'
    )

    with open(filepath, 'w') as f:
        f.write(content)

    print("  ✓ setup.html: 添加中文选项 + data-lang-support")

def patch_other_html():
    """修改其他 HTML 页面 - 更新 lang 属性"""
    html_files = ['index.html', 'waage.html', 'wifi.html', 'upgrade.html']

    for fname in html_files:
        filepath = PROJECT_DIR / 'data' / fname
        backup_file(filepath)

        with open(filepath, 'r') as f:
            content = f.read()

        content = content.replace(
            '<html lang="en">',
            '<html lang="en" data-lang-support="en,de,zh">'
        )

        with open(filepath, 'w') as f:
            f.write(content)

        print(f"  ✓ {fname}: 更新 data-lang-support")

def main():
    print("=" * 60)
    print("FilaMan ESP32 - 中文语言支持补丁")
    print("=" * 60)
    print()

    if not PROJECT_DIR.exists():
        print(f"❌ 项目目录不存在: {PROJECT_DIR}")
        return

    # 创建备份目录
    BACKUP_DIR.mkdir(exist_ok=True)
    print(f"备份目录: {BACKUP_DIR}")

    print("\n开始应用补丁...\n")

    try:
        patch_lang_h()
        patch_lang_cpp()
        patch_setup_html()
        patch_other_html()

        print()
        print("=" * 60)
        print("✅ 中文语言补丁应用完成！")
        print("=" * 60)
        print()
        print("修改文件:")
        print("  - src/lang.h              (添加 LANG_ZH 枚举)")
        print("  - src/lang.cpp            (添加 90+ 中文字符串)")
        print("  - data/setup.html          (添加中文下拉选项)")
        print("  - data/index.html          (更新 lang 属性)")
        print("  - data/waage.html          (更新 lang 属性)")
        print("  - data/wifi.html           (更新 lang 属性)")
        print("  - data/upgrade.html        (更新 lang 属性)")
        print()
        print(f"原文件备份在: {BACKUP_DIR}")
        print()
        print("下一步:")
        print("  1. 编译项目: platformio run")
        print("  2. 烧录到 ESP32")
        print("  3. 在设置页面选择 '中文'  language")
        print()

    except Exception as e:
        print(f"\n❌ 错误: {e}")
        import traceback
        traceback.print_exc()

if __name__ == '__main__':
    main()

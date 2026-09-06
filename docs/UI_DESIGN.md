# 五页彩色行车界面

2026-09-06：五页彩色界面和 BLE 数据入口已使用原 `build` 目录编译并烧录到 COM9。
最新固件和运行验证见 [BLE 烧录记录](ELM327_BLE.md)；本文件末尾保留 BLE 接入前的历史记录。
产品规划和数据来源边界见 [商用副屏产品规格](COMMERCIAL_DISPLAY_SPEC.md)。
`product-ui/car-companion.html` 是设计参考，板端使用原生 LVGL 实现，不运行浏览器。

默认夜间「夜行霓虹」，右上角水滴图标切换夜行霓虹、熔岩赛道、酸性机能。
齿轮设置可修改日间外观、演示模式、涡轮/自吸类型和胎压单位。设置当前保存在内存，
重启恢复默认实时模式；演示模式不会自动开启。

| 页面 | 内容 |
| --- | --- |
| 驾驶 | 大号车速、分段转速带、档位、续航、油量、油耗和四项辅助数据 |
| 车况 | 冷却液、机油、变速箱与进气温度、机油压力、系统电压 |
| 胎压 | 四轮压力与温度、车辆俯视示意；bar/kPa/psi 单位切换 |
| 行程 | 本次/今日/本箱油、里程、时间、油耗、燃油成本和趋势 |
| 性能 | 增压/歧管压力、节气门、负载、横纵向加速度、转速、历史成绩 |

底部五个按钮直接选页，触摸区域为 148 × 48 像素。没有定时自动翻页。
未连接或未支持的数据为 `--`。采集层逐字段执行 2/4/10/15 秒失效策略，断线清空实时数据；
界面还保留整体数据源超过 5 秒不再发布时的兜底清空。连接过程显示「搜索设备 / 连接设备 / 同步设备」。
低油量、高水温和高转速使用文字和颜色共同提示；屏幕不展示协议名称和芯片型号。
当前阈值延续原有设置：油量不高于 15%、水温不低于 105°C、转速不低于 6000 rpm。
这些是显示提示，不是车辆故障诊断结论。

实时模式通过 BLE 接入车速、转速、油量、水温、电压、负载、节气门、进气温度、机油温度和
歧管绝对压力，具体取决于所选 ECU 的支持位图。其他预留值始终为 `--`，不伪造档位、胎压、
保养状态或性能成绩。不会用固定大气压将绝对压力伪装成涡轮增压值。
行程累计、历史存储、油价编辑、车型档案和可配置告警仍属于后续功能，不是本轮交付的采集能力。
演示模式在页头明确标记「演示数据」，所有页面使用固定样例；车辆更新不会覆盖演示画面。
退出演示时恢复仍有效的实时数据，过期值清空。板端使用相同字体和配色意图重新排版，
不承诺与浏览器原型逐像素一致。

## 文件职责

- `main/ui.c`：屏幕、触摸驱动、任务锁以及车辆数据到显示模型的映射。
- `main/dashboard.c`：五页 LVGL 布局、三套配色、手动导航、设置和独立演示数据。
- `main/dashboard.h`：不依赖车辆协议或硬件驱动的显示数据模型。
- `main/dashboard_text.h`：全部中文界面文案。
- `main/fonts/`：生成后直接编入固件的字体及 OFL 授权。

## 中文字体

原有内置 SimSun 是有限字符的子集，不包含所有简体中文文案。现在使用项目
LVGL 9.2.0 自带的 Noto Sans SC 源字体，提取实际使用的字符，生成 18/22 像素
抗锯齿中文字库。字号较大的数字使用 Montserrat。无需给开发板安装额外字体。

新增中文文案后执行：

```powershell
node tools/generate_ui_fonts.mjs
```

转换器固定为 `lv_font_conv@1.5.3`。字体生成不属于日常固件编译的联网依赖；
生成的 C 文件保留在工程中。字体来源与转换格式参见
[Noto](https://notofonts.github.io/noto-docs/website/use/) 和
[LVGL 字体转换器](https://github.com/lvgl/lv_font_conv)。

## 同源渲染验证

`tools/ui_preview` 在电脑上编译相同的 `dashboard.c` 和字体，输出 800 × 480
PNG。验证覆盖等待、正常、极值、低油量、高转速、数据过期、点击导航、三套配色的日夜外观、
演示隔离、单位切换、三种设备连接状态和 150 次重建。此次输出 74 张截图，检查 3196 个可见标签，缺字、裁切、
标签重叠和边界错误均为 0。截图位于 `docs/ui-preview/cyber/`。

```powershell
cmake --build build/ui_preview --parallel 6
New-Item -ItemType Directory -Force docs/ui-preview/cyber
build/ui_preview/dashboard_preview.exe docs/ui-preview/cyber
```

首次配置可使用本机 C 编译器；本次使用放在 `build/host-tools` 内的便携 Zig：

```powershell
cmake -S tools/ui_preview -B build/ui_preview -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:/Espressif/tools/ninja/1.12.1/ninja.exe `
  -DCMAKE_C_COMPILER=C:/projects/odb2-car-display/build/host-tools/zig-windows-x86_64-0.13.0/zig.exe `
  -DCMAKE_C_COMPILER_ARG1=cc -DCMAKE_BUILD_TYPE=Debug
```

固件仍使用原来的 `idf.py -B build build` 和 `idf.py -B build -p COM9 flash`。

原 `build` 绑定了用户目录下的 Python 环境。若安装器 profile 指向另一套 Python，
使用以下环境激活方式，不要为此清空构建目录：

```powershell
$env:IDF_TOOLS_PATH = 'C:\Users\zhang\.espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\zhang\.espressif\python_env\idf5.5_py3.11_env'
. 'C:\esp\v5.5.5\esp-idf\export.ps1'
idf.py -B build build
idf.py -B build -p COM9 flash
```

## 历史烧录：彩色 UI 版本（BLE 接入前）

- 应用：`build/odb2_dash.bin`，821968 字节，原 1 MiB 应用分区剩余 22%。
- SHA-256：`67c498f5569e531c15d3d9c9d3fc56dd81d1e201913b206e6933210c5b9102fd`。
- 写入和校验成功；未清空 NVS，未修改分区布局。
- 启动日志确认 GT911 ID 911、触摸注册成功、五页三配色界面启动以及车辆采集启动。
- 串口观察 22 秒无重启/崩溃；日志在 `build/cyber-boot.log`。物理触摸与显示观感仍需板上验收。

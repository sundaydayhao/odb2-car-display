# 显示板固件（ESP32-P4）

本目录是显示板的完整板级包：[`main/`](main/) 保存全部 ESP-IDF 组件源码；[`hardware/`](hardware/README.md) 保存当前单板的 KiCad 原理图、PCB、器件库、3D 外壳及历史参考工程。内容包括 LVGL 界面、触摸、语音唤醒、影音中心、可选摄像头、SD、Wi-Fi、云服务和 OBD 客户端。

显示板的 ESP-IDF 工程配置、依赖缓存和分区表都保存在本目录；仓库根目录的脚本只负责调用这个工程：

```powershell
.\tools\idf55.ps1 build
.\tools\idf55.ps1 -IdfArgs @('-p', 'COMx', 'flash', 'monitor')
```

本目录的 `CMakeLists.txt` 将 `main` 注册为应用组件，`sdkconfig`、`sdkconfig.defaults`、`partitions.csv`、`managed_components/` 和 `build/` 均属于显示板工程。移动后不会改变组件名、分区布局或烧录参数。

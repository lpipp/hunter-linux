# hunter-linux

HONOR Hunter (HUAWEI FRD-WX9) 的 Linux 支持：查询性能模式与风扇转速，控制 Logo、键盘、风谷灯光。

只包含源码，构建与打包由下游完成。

## 组成

- `driver/`：在上游 `huawei-wmi` 驱动上增加 Hunter 支持，模块名仍为 `huawei_wmi`，原有热键、Fn lock、充电阈值、麦克风灯功能保留。
- `cli/`：命令行工具 `hunter`。
- `tests/`：灯光报文编码测试。

```sh
make            # 构建 hunter 与驱动（KDIR 指定内核构建目录）
make test
```

## 用法

```console
$ hunter mode
1
$ hunter fans
1962 1969
$ hunter light logo on
$ hunter light wind off
$ hunter light keyboard FF0000
$ hunter light keyboard 2 1 FF0000,00FF00,0000FF
```

- `mode`：`0` 均衡，`1` 性能，`2` 安静。
- `fans`：fan1、fan2 转速（RPM）。
- `light ZONE on|off|RRGGBB`：开（默认蓝色 `00AAFF`）、关、单色常亮。
- `light ZONE EFFECT SPEED PALETTE`：灯效编号、速度和逗号分隔的颜色组。

| 灯区 | 灯效 | 颜色数 |
|---|---|---|
| logo | 0、1 | 1 |
| logo | 2 | 1–16 |
| keyboard | 0、1 | 1 或 4（四区各一色） |
| keyboard | 3、4 | 1 |
| keyboard | 5 | 4 |
| keyboard | 2、6、7 | 1–16 |
| wind | 0、1、4、5 | 1 |
| wind | 3 | 4 |
| wind | 2、6、7 | 1–16 |

灯效 `0` 为常亮，速度只能为 `0`；其他灯效速度为 `1`–`5`。

参数错误退出码为 `2`，其他失败为 `1`。

## sysfs 接口

位于 `/sys/bus/platform/devices/huawei-wmi/`：

| 节点 | 权限 | 内容 |
|---|---|---|
| `hunter/mode` | 0444 | 每次读取都向固件查询 |
| `hunter/fans` | 0444 | 同上 |
| `hunter_light/{logo,keyboard,wind}` | 0222 | 写入 `off` 或 `EFFECT SPEED RRGGBB[,RRGGBB...]` |

## 许可证

GPL-2.0。驱动基于 Ayman Bagabas 的 `huawei-wmi`。

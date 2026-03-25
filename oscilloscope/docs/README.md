# ESP32-S3 示波器 — 文档索引

> 生成时间：2026-03-19  
> 分析方法：静态源码分析（全量阅读，无需编译）  
> 覆盖文件：15 个源文件，~2213 行代码

---

## 📋 文档目录

| 文档 | 说明 |
|------|------|
| [DESIGN.md](DESIGN.md) | **主设计文档（SDD）** — 架构概览、HLD/LLD、调用流程、风险、验证计划 |
| [00_inventory.md](00_inventory.md) | 代码盘点 — 文件统计、模块列表、入口函数、依赖清单 |
| [01_architecture.md](01_architecture.md) | 架构分析 — 模块关系图（Mermaid）、数据流、控制流、任务一览 |
| [02_callgraph.md](02_callgraph.md) | 函数调用图 — 完整调用树（文本 + SVG）、JS 调用图、限制说明 |
| [03_design_details.md](03_design_details.md) | 深度逻辑分析 — 数据结构、关键流程、并发分析、边界条件、风险 |

---

## 🗺️ 图表索引

| 图表 | 格式 | 说明 |
|------|------|------|
| [diagrams/callgraph_firmware.svg](diagrams/callgraph_firmware.svg) | SVG | 固件函数调用图（Python 生成） |
| [diagrams/module_dependency.svg](diagrams/module_dependency.svg) | SVG | 模块/文件依赖图（Python 生成） |
| [diagrams/callgraph_firmware.dot](diagrams/callgraph_firmware.dot) | DOT | 固件调用图 Graphviz 源文件 |
| [diagrams/module_dependency.dot](diagrams/module_dependency.dot) | DOT | 模块依赖图 Graphviz 源文件 |
| [diagrams/generate_svg.py](diagrams/generate_svg.py) | Python | SVG 生成脚本（无外部依赖） |

> **重新生成图表**：`python docs/diagrams/generate_svg.py`
>
> **Graphviz 渲染**（需安装 Graphviz）：
> ```bash
> dot -Tsvg docs/diagrams/callgraph_firmware.dot -o docs/diagrams/callgraph_firmware_full.svg
> dot -Tsvg docs/diagrams/module_dependency.dot  -o docs/diagrams/module_dependency_full.svg
> ```

---

## 🔍 快速导航

| 我想了解… | 去哪里看 |
|-----------|----------|
| 项目整体架构 | [DESIGN.md §2-3](DESIGN.md) / [01_architecture.md](01_architecture.md) |
| 函数调用关系 | [02_callgraph.md](02_callgraph.md) + [diagrams/callgraph_firmware.svg](diagrams/callgraph_firmware.svg) |
| ADC 采样如何工作 | [03_design_details.md §2.1](03_design_details.md) |
| WebSocket 数据格式 | [03_design_details.md §1.3](03_design_details.md) |
| 双缓冲并发机制 | [03_design_details.md §3.1](03_design_details.md) |
| 前端波形渲染 | [03_design_details.md §2.5](03_design_details.md) |
| 触发算法 | [03_design_details.md §7](03_design_details.md) |
| 已知风险 | [03_design_details.md §8](03_design_details.md) / [DESIGN.md §8](DESIGN.md) |
| 文件/模块依赖 | [diagrams/module_dependency.svg](diagrams/module_dependency.svg) |
| 有哪些文件 | [00_inventory.md](00_inventory.md) |
| 如何编译烧录 | 项目根目录 [README.md](../README.md) |

---

## 📊 分析统计

| 项目 | 数量 |
|------|------|
| 分析文件总数 | 15 |
| C 源文件 | 4（697 行） |
| C 头文件 | 4（76 行） |
| JavaScript | 1（647 行） |
| HTML | 1（220 行） |
| CSS | 1（498 行） |
| 固件函数总数 | 19（含 static） |
| JS 函数总数 | ~22 |
| FreeRTOS 任务数 | 2（用户创建） |
| HTTP 路由数 | 6 |
| 已识别风险 | 8 |
| 待确认问题 | 8 |

---

## ⚠️ 工具可用性

| 工具 | 状态 | 影响 |
|------|------|------|
| Graphviz `dot` | ❌ 未安装 | DOT 文件已生成，SVG 由 Python 手动生成 |
| Doxygen | ❌ 未安装 | 无 Doxygen HTML 输出 |
| `pyan3` | N/A | 无 Python 业务源码 |

**替代方案**：
- SVG 图表：`python docs/diagrams/generate_svg.py`
- Graphviz 完整版：安装 Graphviz 后执行上方命令
- Doxygen：安装后使用 `docs/Doxyfile`（待创建）

---

## 🔧 如何复现分析

```bash
# 1. 重新生成 SVG 图表（无需额外依赖）
python docs/diagrams/generate_svg.py

# 2. 若已安装 Graphviz，生成完整调用图
dot -Tsvg docs/diagrams/callgraph_firmware.dot -o docs/diagrams/callgraph_firmware_full.svg
dot -Tsvg docs/diagrams/module_dependency.dot  -o docs/diagrams/module_dependency_full.svg

# 3. 若已安装 Doxygen，生成 HTML 文档
# doxygen docs/Doxyfile  （Doxyfile 待创建）
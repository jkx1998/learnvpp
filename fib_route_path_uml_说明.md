# FIB Route Path 结构体 UML 图说明

## 文件信息
- **文件名**: `fib_route_path_uml.xml`
- **格式**: draw.io (diagrams.net) XML 格式
- **用途**: 在 draw.io 中编辑和查看 FIB Route Path 结构体的 UML 类图

## 结构体概述

`fib_route_path_t` 是 VPP (Vector Packet Processing) 中 FIB (Forwarding Information Base) 模块的核心结构体，用于表示路由路径的各种类型。

## UML 图结构

### 1. 主结构体: fib_route_path_t
- **frp_proto**: dpo_proto_t - 地址协议类型
- **union**: 路径类型联合体（只能选择其中一个）
- **frp_weight**: u8 - 路径权重
- **frp_preference**: u8 - 路径优先级（0为最佳）
- **frp_flags**: fib_route_path_flags_t - 路径标志位

### 2. Union 结构体
包含多种路径类型，只能选择其中一个：

#### 2.1 嵌套结构体 (struct)
- **嵌套 union**: 地址/标签信息
- **frp_sw_if_index**: u32 - 接口索引
- **frp_rpf_id**: fib_rpf_id_t - RPF-ID
- **frp_fib_index**: u32 - FIB 索引
- **frp_label_stack**: fib_mpls_label_t* - MPLS 标签栈
- **dpo**: dpo_id_t - 专用 DPO
- **frp_mitf_flags**: u32 - MFIB 接口标志

#### 2.2 其他路径类型
- **frp_bier_tbl**: bier_table_id_t - BIER 表路径
- **frp_udp_encap_id**: u32 - UDP 封装路径
- **frp_classify_table_id**: u32 - 分类表路径
- **frp_bier_fmask**: index_t - BIER Fmask 路径
- **frp_dpo**: dpo_id_t - 专用 DPO 路径

### 3. 嵌套 Union
在嵌套结构体中的 union，包含：

- **frp_addr**: ip46_address_t - 下一跳地址
- **MPLS 结构体**: 
  - frp_local_label: mpls_label_t - MPLS 本地标签
  - frp_eos: mpls_eos_bit_t - EOS 位
- **frp_bier_imp**: index_t - BIER 封装路径
- **frp_connected**: fib_prefix_t - 连接前缀（Glean 路径）

## 在 draw.io 中的使用方法

1. 打开 [draw.io](https://app.diagrams.net/) 网站
2. 选择 "文件" → "打开" → "设备"
3. 选择 `fib_route_path_uml.xml` 文件
4. 图将自动加载，可以：
   - 调整布局和大小
   - 修改颜色和样式
   - 添加注释和说明
   - 导出为 PNG、SVG 等格式

## 路径类型说明

根据 `frp_flags` 的不同组合，路径可以分为：

1. **Attached-next-hop**: 连接下一跳路径
2. **Attached**: 连接路径（需要 ARP）
3. **Recursive**: 递归路径
4. **Deaggregate (deag)**: 解聚合路径
5. **BIER 相关路径**: BIER 表、Fmask、封装路径
6. **UDP 封装路径**: UDP 封装
7. **分类路径**: 分类表路径

## 技术要点

- Union 表示互斥的路径类型选择
- 嵌套结构体显示复杂的字段关系
- 实线箭头表示包含关系
- 所有字段都包含数据类型信息

## 相关文件

- 源文件: `../../../usr/include/vnet/fib/fib_types.h`
- 实现文件: `src/vnet/fib/fib_path_list.c`

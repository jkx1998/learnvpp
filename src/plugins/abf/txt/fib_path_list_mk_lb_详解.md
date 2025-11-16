# FIB 路径列表负载均衡创建代码详解

## 一、代码段概述

```c
/*
 * We gather the DPOs from resolved paths.
 */
vec_foreach (path_index, path_list->fpl_paths)
{
    if ((flags & FIB_PATH_LIST_FWD_FLAG_STICKY) ||
        fib_path_is_resolved(*path_index))
    {
        nhs = fib_path_append_nh_for_multipath_hash(
            *path_index, fct,
            fib_forw_chain_type_to_dpo_proto(fct),
            nhs);
    }
}
```

这段代码位于 `fib_path_list_mk_lb` 函数中，是 VPP FIB (Forwarding Information Base) 模块的核心部分，负责**从已解析的路径中收集 DPO (Data-Path Object)** 来构建负载均衡对象。

---

## 二、背景知识

### 2.1 FIB 路径列表 (fib_path_list_t)

**路径列表**是 FIB 中表示一个前缀可以通过的所有路径的集合：

```c
typedef struct fib_path_list_t_ {
    fib_node_t fpl_node;           // FIB 图节点
    fib_path_list_flags_t fpl_flags; // 路径列表标志
    fib_node_index_t *fpl_paths;   // 路径索引向量
    fib_node_index_t fpl_urpf;     // uRPF 列表
} fib_path_list_t;
```

**关键概念**：
- 一个前缀可以有多个路径（多路径路由）
- 路径列表管理这些路径的集合
- 负载均衡在这些路径之间分配流量

### 2.2 路径类型 (fib_path_t)

路径有多种类型，每种类型有不同的解析方式：

```c
typedef enum fib_path_type_t_ {
    FIB_PATH_TYPE_ATTACHED_NEXT_HOP,  // 直连下一跳
    FIB_PATH_TYPE_ATTACHED,           // 直连
    FIB_PATH_TYPE_RECURSIVE,          // 递归路径
    FIB_PATH_TYPE_SPECIAL,            // 特殊路径（丢弃等）
    FIB_PATH_TYPE_EXCLUSIVE,          // 独占路径
    FIB_PATH_TYPE_DEAG,               // 解聚合路径
    FIB_PATH_TYPE_INTF_RX,            // 接口接收
    FIB_PATH_TYPE_UDP_ENCAP,          // UDP 封装
    FIB_PATH_TYPE_RECEIVE,            // 接收路径
    FIB_PATH_TYPE_BIER_IMP,           // BIER 封装
    FIB_PATH_TYPE_BIER_FMASK,         // BIER FMask
    FIB_PATH_TYPE_BIER_TABLE,         // BIER 表
    FIB_PATH_TYPE_DVR,                // DVR 路径
} fib_path_type_t;
```

---

## 三、代码逐行解析

### 3.1 循环遍历所有路径

```c
vec_foreach (path_index, path_list->fpl_paths)
```

**功能**：遍历路径列表中的所有路径索引

**数据结构**：
- `path_list->fpl_paths`：路径索引的向量（动态数组）
- `path_index`：指向当前路径索引的指针
- `*path_index`：路径的实际索引值

**VPP 向量操作**：
```c
// vec_foreach 宏展开后类似：
for (path_index = path_list->fpl_paths; 
     path_index < vec_end(path_list->fpl_paths); 
     path_index++)
```

### 3.2 路径选择条件

```c
if ((flags & FIB_PATH_LIST_FWD_FLAG_STICKY) ||
    fib_path_is_resolved(*path_index))
```

**条件逻辑**：路径被选中的两个条件：

#### 条件 1：STICKY 标志
```c
flags & FIB_PATH_LIST_FWD_FLAG_STICKY
```

**STICKY 路径的特点**：
- **粘性路径**：即使路径未解析，也包含在负载均衡中
- **使用场景**：
  - 需要保持路径在负载均衡中，即使它暂时不可用
  - 避免负载均衡频繁变化
  - 某些特殊路由策略

#### 条件 2：路径已解析
```c
fib_path_is_resolved(*path_index)
```

**路径解析状态检查**：
```c
int fib_path_is_resolved(fib_node_index_t path_index)
{
    fib_path_t *path = fib_path_get(path_index);
    
    return (dpo_id_is_valid(&path->fp_dpo) &&           // DPO 有效
            (path->fp_oper_flags & FIB_PATH_OPER_FLAG_RESOLVED) && // 操作标志为已解析
            !fib_path_is_looped(path_index) &&          // 不是循环路径
            !fib_path_is_permanent_drop(path));         // 不是永久丢弃
}
```

**路径解析的含义**：
- 路径已经找到对应的转发对象（邻接、负载均衡等）
- 路径处于可用状态
- 路径没有形成递归循环
- 路径没有被配置为永久丢弃

### 3.3 收集路径信息

```c
nhs = fib_path_append_nh_for_multipath_hash(
    *path_index, fct,
    fib_forw_chain_type_to_dpo_proto(fct),
    nhs);
```

**函数调用**：`fib_path_append_nh_for_multipath_hash`

#### 参数详解：

| 参数 | 类型 | 说明 |
|------|------|------|
| `*path_index` | `fib_node_index_t` | **路径索引** - 要处理的路径 |
| `fct` | `fib_forward_chain_type_t` | **转发链类型** - 指定转发协议类型 |
| `fib_forw_chain_type_to_dpo_proto(fct)` | `dpo_proto_t` | **DPO 协议类型** - 从转发链类型转换而来 |
| `nhs` | `load_balance_path_t *` | **下一跳向量** - 用于累积所有路径信息 |

#### 转发链类型 (fct)：
```c
typedef enum fib_forward_chain_type_t_ {
    FIB_FORW_CHAIN_TYPE_UNICAST_IP4,    // IPv4 单播
    FIB_FORW_CHAIN_TYPE_UNICAST_IP6,    // IPv6 单播
    FIB_FORW_CHAIN_TYPE_MPLS_EOS,       // MPLS 栈底
    FIB_FORW_CHAIN_TYPE_MPLS_NON_EOS,   // MPLS 非栈底
    FIB_FORW_CHAIN_TYPE_ETHERNET,       // 以太网
    FIB_FORW_CHAIN_TYPE_NSH,            // 网络服务头
    FIB_FORW_CHAIN_TYPE_MCAST_IP4,      // IPv4 组播
    FIB_FORW_CHAIN_TYPE_MCAST_IP6,      // IPv6 组播
    FIB_FORW_CHAIN_TYPE_BIER,           // BIER
} fib_forward_chain_type_t;
```

---

## 四、`fib_path_append_nh_for_multipath_hash` 函数详解

### 4.1 函数原型

```c
load_balance_path_t *
fib_path_append_nh_for_multipath_hash(fib_node_index_t path_index,
                                      fib_forward_chain_type_t fct,
                                      dpo_proto_t payload_proto,
                                      load_balance_path_t *hash_key)
```

### 4.2 函数实现核心逻辑

```c
load_balance_path_t *
fib_path_append_nh_for_multipath_hash(fib_node_index_t path_index,
                                      fib_forward_chain_type_t fct,
                                      dpo_proto_t payload_proto,
                                      load_balance_path_t *hash_key)
{
    load_balance_path_t *mnh;
    fib_path_t *path;

    path = fib_path_get(path_index);
    ASSERT(path);

    // 在向量末尾添加一个新的负载均衡路径
    vec_add2(hash_key, mnh, 1);

    // 设置路径权重
    mnh->path_weight = path->fp_weight;
    
    // 设置路径索引
    mnh->path_index = path_index;

    // 如果路径已解析，获取其 DPO；否则使用丢弃 DPO
    if (fib_path_is_resolved(path_index))
    {
        fib_path_contribute_forwarding(path_index, fct, payload_proto, &mnh->path_dpo);
    }
    else
    {
        dpo_copy(&mnh->path_dpo,
                 drop_dpo_get(fib_forw_chain_type_to_dpo_proto(fct)));
    }
    return (hash_key);
}
```

### 4.3 负载均衡路径结构

```c
typedef struct load_balance_path_t_
{
    /**
     * The weight of the path
     */
    u16 path_weight;
    
    /**
     * The index of the path in the FIB
     */
    fib_node_index_t path_index;
    
    /**
     * The DPO to which the path resolves
     */
    dpo_id_t path_dpo;
} load_balance_path_t;
```

**关键字段**：
- `path_weight`：路径权重，用于 UCMP (Unequal Cost Multi-Path)
- `path_index`：FIB 中的路径索引，用于调试和跟踪
- `path_dpo`：路径解析后的 DPO，实际转发对象

---

## 五、完整上下文分析

### 5.1 `fib_path_list_mk_lb` 完整函数

```c
static void
fib_path_list_mk_lb(fib_path_list_t *path_list,
                    fib_forward_chain_type_t fct,
                    dpo_id_t *dpo,
                    fib_path_list_fwd_flags_t flags)
{
    fib_node_index_t *path_index;
    load_balance_path_t *nhs;
    dpo_proto_t dproto;

    nhs = NULL;
    dproto = fib_forw_chain_type_to_dpo_proto(fct);

    /*
     * We gather the DPOs from resolved paths.
     */
    vec_foreach(path_index, path_list->fpl_paths)
    {
        if ((flags & FIB_PATH_LIST_FWD_FLAG_STICKY) ||
            fib_path_is_resolved(*path_index))
        {
            nhs = fib_path_append_nh_for_multipath_hash(
                *path_index, fct,
                fib_forw_chain_type_to_dpo_proto(fct),
                nhs);
        }
    }

    /*
     * Path-list load-balances, which if used, would be shared and hence
     * never need a load-balance map.
     */
    dpo_set(dpo,
            DPO_LOAD_BALANCE,
            dproto,
            load_balance_create(vec_len(nhs),
                                dproto,
                                load_balance_get_default_flow_hash(dproto)));
    load_balance_multipath_update(dpo, nhs,
                                  fib_path_list_fwd_flags_2_load_balance(flags));

    FIB_PATH_LIST_DBG(path_list, "mk lb: %d", dpo->dpoi_index);

    vec_free(nhs);
}
```

### 5.2 后续处理步骤

1. **创建负载均衡对象**：
   ```c
   dpo_set(dpo, DPO_LOAD_BALANCE, dproto,
           load_balance_create(vec_len(nhs), dproto,
                              load_balance_get_default_flow_hash(dproto)));
   ```

2. **更新多路径信息**：
   ```c
   load_balance_multipath_update(dpo, nhs,
                                 fib_path_list_fwd_flags_2_load_balance(flags));
   ```

3. **清理资源**：
   ```c
   vec_free(nhs);
   ```

---

## 六、设计哲学和优化考虑

### 6.1 性能优化

**向量操作的效率**：
- `vec_add2` 是高效的向量扩展操作
- 向量在内存中连续存储，具有良好的缓存局部性
- 一次性分配多个元素，减少内存分配次数

**条件检查的顺序**：
```c
if ((flags & FIB_PATH_LIST_FWD_FLAG_STICKY) ||
    fib_path_is_resolved(*path_index))
```

**优化考虑**：
- `flags` 检查是简单的位操作，非常快速
- `fib_path_is_resolved` 需要内存访问和多个条件检查
- 将快速检查放在前面，避免不必要的函数调用

### 6.2 错误处理

**未解析路径的处理**：
```c
if (fib_path_is_resolved(path_index))
{
    fib_path_contribute_forwarding(...);
}
else
{
    dpo_copy(&mnh->path_dpo, drop_dpo_get(...));
}
```

**设计原则**：
- 未解析的路径使用丢弃 DPO，确保数据包不会错误转发
- 保持负载均衡结构的完整性，即使某些路径不可用
- 允许路径在后续解析后自动更新

### 6.3 内存管理

**向量生命周期**：
- `nhs` 向量在函数内部动态构建
- 函数结束时通过 `vec_free(nhs)` 释放
- 避免内存泄漏

---

## 七、实际应用场景

### 7.1 多路径路由示例

假设有以下路由配置：
```
10.0.0.0/24
  via 192.168.1.1 weight 10 (resolved)
  via 192.168.1.2 weight 20 (resolved) 
  via 192.168.1.3 weight 15 (unresolved)
```

**代码执行过程**：
1. 遍历三个路径索引
2. 路径1：已解析 → 添加到 `nhs`
3. 路径2：已解析 → 添加到 `nhs`  
4. 路径3：未解析 → 跳过（除非 STICKY 标志设置）
5. 创建包含2个桶的负载均衡
6. 根据权重分配流量（10:20 比例）

### 7.2 STICKY 标志使用场景

**BGP 多路径**：
- 即使某些路径暂时不可达，也保持它们在负载均衡中
- 避免路由震荡导致的频繁负载均衡重建
- 提供更稳定的流量工程

---

## 八、调试和监控

### 8.1 调试输出

```c
FIB_PATH_LIST_DBG(path_list, "mk lb: %d", dpo->dpoi_index);
```

**调试信息**：
- 路径列表标识
- 创建的负载均衡对象索引
- 可用于跟踪负载均衡创建过程

### 8.2 CLI 命令

```bash
# 查看路径列表
vpp# show fib path-lists

# 查看特定路径
vpp# show fib paths

# 查看负载均衡
vpp# show load-balance
```

---

## 九、总结

### 9.1 核心功能

1. **路径收集**：从路径列表中筛选可用的路径
2. **DPO 构建**：为每个路径获取对应的转发对象
3. **负载均衡创建**：基于收集的路径信息构建负载均衡

### 9.2 关键设计点

- **条件筛选**：只包含已解析或 STICKY 路径
- **错误容忍**：未解析路径使用丢弃 DPO
- **性能优化**：高效的向量操作和条件检查顺序
- **资源管理**：正确释放动态分配的向量

### 9.3 在 VPP 转发体系中的位置

```
FIB Entry
  ↓
Path List (此代码所在位置)
  ↓
Load Balance
  ↓
Adjacency / Next DPO
  ↓
Data Plane Forwarding
```

这段代码是 VPP 多路径路由转发的核心，负责将路由策略转换为实际的数据平面转发结构。

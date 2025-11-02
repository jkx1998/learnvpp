/*
 * Copyright (c) 2017 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <plugins/abf/abf_policy.h>

#include <vlib/vlib.h>
#include <vnet/plugin/plugin.h>
#include <vnet/fib/fib_path_list.h>
#include <vnet/fib/fib_walk.h>

/**
 * FIB node type the attachment is registered
 */
fib_node_type_t abf_policy_fib_node_type;

/**
 * Pool of ABF objects
 * 声明了一个名为 abf_policy_pool 的静态指针变量。该指针用于指向一个或多个 abf_policy_t 类型的内存区域。
 */
static abf_policy_t *abf_policy_pool;

/**
 * DB of ABF policy objects
 *  - policy ID to index conversion.
 */
static uword *abf_policy_db;


abf_policy_t *
abf_policy_get (u32 index)
{
  return (pool_elt_at_index (abf_policy_pool, index));
}

static u32
abf_policy_get_index (const abf_policy_t * abf)
{
  return (abf - abf_policy_pool);
}

static abf_policy_t *
abf_policy_find_i (u32 policy_id)
{
  u32 api;

  api = abf_policy_find (policy_id);

  if (INDEX_INVALID != api)
    return (abf_policy_get (api));

  return (NULL);
}

u32
abf_policy_find (u32 policy_id)
{
  uword *p;
  //从db数据库中获取key的值的指针
  p = hash_get (abf_policy_db, policy_id);

  if (NULL != p)
    return (p[0]);// 如果找到，返回哈希表中存储的值（策略索引）
    //p[0] 是为了从指针获取实际存储的值，而 p 只是指向那个值的地址。
    // 三个不同的概念：
    // 1. p        - 指针变量，存储的是数据的地址
    // 2. p[0]、*p - 指针指向的数据（实际值）
    // 3. &p       - 指针变量本身的地址
  return (INDEX_INVALID);
}


int
abf_policy_update (u32 policy_id,
		   u32 acl_index, const fib_route_path_t * rpaths)
{
  /*QA: ap 和 abf_policy_pool不都是policy_t的静态指针吗？为甚需要两者相减得到一个新的policy_id?
   *- `abf_policy_pool` 指向池的起始地址
    - `ap` 指向池中某个具体元素的地址
    - 两者相减得到的是该元素在池中的索引位置
      这类似于数组索引：`&array[i] - array = i`
   */


  abf_policy_t *ap;
  u32 api;

  api = abf_policy_find (policy_id);

  if (INDEX_INVALID == api)
    {
      /*
       * create a new policy
       * QA:pool_get的用法？
       *    - 从 `abf_policy_pool` 池中分配一个新的元素
            - 将新元素的地址存储在 `ap` 指针中
            - 如果池中没有空闲元素，会自动扩展池的大小

       * Q:fib_node_init是什么意思？这使ABF策略能够参与到FIB的依赖图系统中？

       * Q:fib_path_list_create函数解读？
       */
      pool_get (abf_policy_pool, ap);

      //将内存地址转换为池中的索引位置
      api = ap - abf_policy_pool;
      //这是一个 FIB 节点的初始化函数，用于设置新创建的 FIB 节点的初始状态。fib_node_t
      fib_node_init (&ap->ap_node, abf_policy_fib_node_type);
      //abf_policy_t init 
      ap->ap_acl = acl_index;
      ap->ap_id = policy_id;
      //用于创建 FIB路径列表，这是 VPP 转发系统的核心组件，用于管理数据包的转发路径。
      ap->ap_pl = fib_path_list_create ((FIB_PATH_LIST_FLAG_SHARED |
					 FIB_PATH_LIST_FLAG_NO_URPF), rpaths);

      /*
       * become a child of the path list so we get poked when
       * the forwarding changes.
       * QA:为啥要为添加路径列表字节点？什么是路径列表子节点？
       * - 当路径列表发生变化时（如路径添加/删除），需要通知所有依赖它的对象
       * - ABF策略作为路径列表的子节点，当路径列表变化时会收到通知
       * - `ap->ap_sibling` 存储子节点关系标识，用于后续移除关系
        这个函数用于向路径列表添加子节点，实现 FIB 系统中的依赖关系管理，并包含一个重要的性能优化机制。
       */
      ap->ap_sibling = fib_path_list_child_add (ap->ap_pl,
						abf_policy_fib_node_type,
						api);

      /*
       * add this new policy to the DB
       * QA：实现的原理是，怎么添加到数据库，数据库张什么样子？
       * - `abf_policy_db` 是一个哈希表，键是policy_id，值是池索引api
       * - 实现policy_id到池索引的快速查找
       * - 哈希表结构：`policy_id -> api` 的映射关系

       */
      hash_set (abf_policy_db, policy_id, api);

      /*
       * take a lock on behalf of the CLI/API creation
       * QA：为啥要锁定fib节点？
       * - 防止在配置过程中策略被意外删除
       * - 每个CLI/API创建都会持有一个锁
       * - 只有当所有锁都释放时，策略才能被销毁

       */
      fib_node_lock (&ap->ap_node);
    }
  else
    {
      /*
       * update an existing policy.
       * - add the path to the path-list and swap our ancestry
       * - backwalk to poke all attachments to update
       * QA：abf_policy_get如何实现？
       * - 使用 `pool_elt_at_index` 根据索引从池中获取元素
       * - 这是VPP内存池的标准访问方式

       */
      fib_node_index_t old_pl;

      ap = abf_policy_get (api);
      old_pl = ap->ap_pl;
      if (ap->ap_acl != acl_index)
	{
	  /* Should change this error code to something more descriptive */
	  return (VNET_API_ERROR_INVALID_VALUE);
	}

      if (FIB_NODE_INDEX_INVALID != old_pl)
	{
    //Q：复制路径列表添加新的路径，为啥是复制路径列表而不是直接添加新的路径列表，这个路径列表张什么样子？
    //复制后确保不影响其他使用者
    //- 包含多个转发路径（nexthop、接口、权重等）
    //- 支持负载均衡和故障切换

	  ap->ap_pl = fib_path_list_copy_and_path_add (old_pl,
						       (FIB_PATH_LIST_FLAG_SHARED
							|
							FIB_PATH_LIST_FLAG_NO_URPF),
						       rpaths);
    //QA：移除旧的子节点关系，子节点是什么？为什么要移除？路径列表和子节点关系有什么联系
    /*
    - 当ABF策略引用路径列表时，它成为路径列表的"子节点"
    - 当路径列表变化时，所有子节点都会收到通知
    - 移除旧的子节点关系是因为路径列表已经改变，需要建立新的依赖关系
    */
	  fib_path_list_child_remove (old_pl, ap->ap_sibling);
	}
      else
	{
	  ap->ap_pl = fib_path_list_create ((FIB_PATH_LIST_FLAG_SHARED |
					     FIB_PATH_LIST_FLAG_NO_URPF),
					    rpaths);
	}

      ap->ap_sibling = fib_path_list_child_add (ap->ap_pl,
						abf_policy_fib_node_type,
						api);

      fib_node_back_walk_ctx_t ctx = {
	.fnbw_reason = FIB_NODE_BW_REASON_FLAG_EVALUATE,
      };

      fib_walk_sync (abf_policy_fib_node_type, api, &ctx);
    }
  return (0);
}

static void
abf_policy_destroy (abf_policy_t * ap)
{
  /*
   * this ABF should not be a sibling on the path list, since
   * that was removed when the API config went
   */
  ASSERT (ap->ap_sibling == ~0);
  ASSERT (ap->ap_pl == FIB_NODE_INDEX_INVALID);

  hash_unset (abf_policy_db, ap->ap_id);
  pool_put (abf_policy_pool, ap);
}

int
abf_policy_delete (u32 policy_id, const fib_route_path_t * rpaths)
{
  abf_policy_t *ap;
  u32 api;

  api = abf_policy_find (policy_id);

  if (INDEX_INVALID == api)
    {
      /*
       * no such policy
       */
      return (VNET_API_ERROR_INVALID_VALUE);
    }

  /*
   * update an existing policy.
   * - add the path to the path-list and swap our ancestry
   * - backwalk to poke all attachments to update
   */
  fib_node_index_t old_pl;

  ap = abf_policy_get (api);
  old_pl = ap->ap_pl;

  /*
   *Q:为什么要锁定路径列表？fib_path_list_lock (old_pl)
      - __防止竞态条件__：在修改路径列表期间，防止其他线程同时访问或修改同一个路径列表
      - __确保数据一致性__：在复制和移除路径的过程中，确保路径列表的状态是一致的
      - __引用计数管理__：锁定会增加引用计数，防止路径列表在操作过程中被意外释放
  */
  fib_path_list_lock (old_pl);
  ap->ap_pl = fib_path_list_copy_and_path_remove (
    ap->ap_pl, (FIB_PATH_LIST_FLAG_SHARED | FIB_PATH_LIST_FLAG_NO_URPF),
    rpaths);

  fib_path_list_child_remove (old_pl, ap->ap_sibling);
  ap->ap_sibling = ~0;

  if (FIB_NODE_INDEX_INVALID == ap->ap_pl)
    {
      /*
       * no more paths on this policy. It's toast
       * remove the CLI/API's lock
       * Q：上面代码并没有看出来锁ap_node,但是为什么这里有解锁ap_node的动作？
            这个锁是在策略创建时设置的，不是在删除函数中设置的
       */
      fib_node_unlock (&ap->ap_node);
    }
  else
    {
      //Q：我没理解else后的逻辑？
      /*
      - 重新建立与新路径列表的子节点依赖关系
      - 通过回走机制触发FIB系统中所有依赖对象的重新评估
      - 保持策略的活跃状态
      */
      ap->ap_sibling =
	fib_path_list_child_add (ap->ap_pl, abf_policy_fib_node_type, api);

      fib_node_back_walk_ctx_t ctx = {
	.fnbw_reason = FIB_NODE_BW_REASON_FLAG_EVALUATE,
      };

      fib_walk_sync (abf_policy_fib_node_type, api, &ctx);
    }
  fib_path_list_unlock (old_pl);

  return (0);
}

static clib_error_t *
abf_policy_cmd (vlib_main_t * vm,
		unformat_input_t * main_input, vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  fib_route_path_t *rpaths = NULL, rpath;
  u32 acl_index, policy_id, is_del;
  dpo_proto_t payload_proto;
  int rv = 0;

  is_del = 0;
  acl_index = INDEX_INVALID;
  policy_id = INDEX_INVALID;

  /* Get a line of input. */
  if (!unformat_user (main_input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "acl %d", &acl_index))
	;
      else if (unformat (line_input, "id %d", &policy_id))
	;
      else if (unformat (line_input, "del"))
	is_del = 1;
      else if (unformat (line_input, "add"))
	is_del = 0;
      else if (unformat (line_input, "via %U",
			 unformat_fib_route_path, &rpath, &payload_proto))
	vec_add1 (rpaths, rpath);
      else
	{
	  clib_error_t *err;
	  err = clib_error_return (0, "unknown input '%U'",
				   format_unformat_error, line_input);
	  unformat_free (line_input);
	  return err;
	}
    }

  if (INDEX_INVALID == policy_id)
    {
      vlib_cli_output (vm, "Specify a Policy ID");
      goto out;
    }

  if (vec_len (rpaths) == 0)
    {
      vlib_cli_output (vm, "Hop path must not be empty");
      goto out;
    }

  if (!is_del)
    {
      if (INDEX_INVALID == acl_index)
	{
	  vlib_cli_output (vm, "ACL index must be set");
	  goto out;
	}

      rv = abf_policy_update (policy_id, acl_index, rpaths);
      /* Should change this error code to something more descriptive */
      if (rv == VNET_API_ERROR_INVALID_VALUE)
	{
	  vlib_cli_output (vm,
			   "ACL index must match existing ACL index in policy");
	  goto out;
	}
    }
  else
    {
      abf_policy_delete (policy_id, rpaths);
    }

out:
  unformat_free (line_input);
  return (NULL);
}

/**
 * Create an ABF policy.
 */
VLIB_CLI_COMMAND (abf_policy_cmd_node, static) = {
  .path = "abf policy",
  .function = abf_policy_cmd,
  .short_help = "abf policy [add|del] id <index> acl <index> via ...",
  .is_mp_safe = 1,
};

static u8 *
format_abf (u8 * s, va_list * args)
{
  abf_policy_t *ap = va_arg (*args, abf_policy_t *);

  s = format (s, "abf:[%d]: policy:%d acl:%d",
	      ap - abf_policy_pool, ap->ap_id, ap->ap_acl);
  s = format (s, "\n ");
  if (FIB_NODE_INDEX_INVALID == ap->ap_pl)
    {
      s = format (s, "no forwarding");
    }
  else
    {
      s = fib_path_list_format (ap->ap_pl, s);
    }

  return (s);
}

void
abf_policy_walk (abf_policy_walk_cb_t cb, void *ctx)
{
  u32 api;

  pool_foreach_index (api, abf_policy_pool)
   {
    if (!cb(api, ctx))
      break;
  }
}

static clib_error_t *
abf_show_policy_cmd (vlib_main_t * vm,
		     unformat_input_t * input, vlib_cli_command_t * cmd)
{
  u32 policy_id;
  abf_policy_t *ap;

  policy_id = INDEX_INVALID;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "%d", &policy_id))
	;
      else
	return (clib_error_return (0, "unknown input '%U'",
				   format_unformat_error, input));
    }

  if (INDEX_INVALID == policy_id)
    {
      pool_foreach (ap, abf_policy_pool)
       {
        vlib_cli_output(vm, "%U", format_abf, ap);
      }
    }
  else
    {
      ap = abf_policy_find_i (policy_id);

      if (NULL != ap)
	vlib_cli_output (vm, "%U", format_abf, ap);
      else
	vlib_cli_output (vm, "Invalid policy ID:%d", policy_id);
    }

  return (NULL);
}

VLIB_CLI_COMMAND (abf_policy_show_policy_cmd_node, static) = {
  .path = "show abf policy",
  .function = abf_show_policy_cmd,
  .short_help = "show abf policy <value>",
  .is_mp_safe = 1,
};

static fib_node_t *
abf_policy_get_node (fib_node_index_t index)
{
  abf_policy_t *ap = abf_policy_get (index);
  return (&(ap->ap_node));
}

static abf_policy_t *
abf_policy_get_from_node (fib_node_t * node)
{
  return ((abf_policy_t *) (((char *) node) -
			    STRUCT_OFFSET_OF (abf_policy_t, ap_node)));
}

static void
abf_policy_last_lock_gone (fib_node_t * node)
{
  abf_policy_destroy (abf_policy_get_from_node (node));
}

/*
 * A back walk has reached this ABF policy
 */
static fib_node_back_walk_rc_t
abf_policy_back_walk_notify (fib_node_t * node,
			     fib_node_back_walk_ctx_t * ctx)
{
  /*
   * re-stack the fmask on the n-eos of the via
   */
  abf_policy_t *abf = abf_policy_get_from_node (node);

  /*
   * propagate further up the graph.
   * we can do this synchronously since the fan out is small.
   */
  fib_walk_sync (abf_policy_fib_node_type, abf_policy_get_index (abf), ctx);

  return (FIB_NODE_BACK_WALK_CONTINUE);
}

/*
 * The BIER fmask's graph node virtual function table
 */
static const fib_node_vft_t abf_policy_vft = {
  .fnv_get = abf_policy_get_node,
  .fnv_last_lock = abf_policy_last_lock_gone,
  .fnv_back_walk = abf_policy_back_walk_notify,
};

static clib_error_t *
abf_policy_init (vlib_main_t * vm)
{
  abf_policy_fib_node_type =
    fib_node_register_new_type ("abf-policy", &abf_policy_vft);

  return (NULL);
}

VLIB_INIT_FUNCTION (abf_policy_init);

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */

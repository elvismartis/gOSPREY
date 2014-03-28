#pragma once
const char *ASTAR_KERNEL_SOURCE =
"#ifndef NUM_LEVEL\n"
"# define NUM_LEVEL 24\n"
"#endif\n"
"#ifndef HEAP_SIZE\n"
"# define HEAP_SIZE 997\n"
"#endif\n"
"#ifndef NUM_ROTAMER\n"
"# define NUM_ROTAMER 100\n"
"#endif\n"
"#ifndef NUM_MAX_CHILD\n"
"# define NUM_MAX_CHILD 35\n"
"#endif\n"
"#ifndef GROUP_SIZE\n"
"# define GROUP_SIZE 256\n"
"#endif\n"
"\n"
"#define nVERBOSE_DEBUG\n"
"\n"
"#define SMP (CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST)\n"
"#define PR  (__constant char *)\n"
"\n"
"typedef struct data_t {\n"
"    float f_score;\n"
"    float g_score;\n"
"    int level;\n"
"    uchar conf[NUM_LEVEL];\n"
"} data_t;\n"
"\n"
"typedef struct heap_t {\n"
"    float v;\n"
"    int idx;\n"
"} heap_t;\n"
"\n"
"uint flip_float(float f)\n"
"{\n"
"    int u = as_int(f);\n"
"    return u ^ ((u >> 31) | 0x80000000);\n"
"}\n"
"\n"
"void print_node(int level, float f_score, float g_score, int conf[])\n"
"{\n"
"    printf(PR\"level: %d\\t f_score: %.4f g_score: %.4f\\n\",\n"
"           level, f_score, g_score);\n"
"    for (int i = 0; i <= level; ++i)\n"
"        printf(PR\"%d \", conf[i]);\n"
"    printf(PR\"\\n\");\n"
"}\n"
"\n"
"/* return a pair of int (fscore, gscore) */\n"
"float2 __compute_score(int *conf,\n"
"                       int level,\n"
"                       float old_g_score,\n"
"                       const __local ushort node_offset[],\n"
"                       const __local uchar  rot_per_level[],\n"
"                       const __local float  self_energy[],\n"
"                       __read_only image2d_t reduce_energy,\n"
"                       __read_only image2d_t pm_energy)\n"
"{\n"
"    sampler_t sampler = SMP;\n"
"    float g_score = old_g_score; /* use previous g score */\n"
"    float h_score = 0.f;\n"
"\n"
"    /* compute g delta */\n"
"    {\n"
"        int idx = node_offset[level] + conf[level];\n"
"        for (int j = 0; j < level; ++j) {\n"
"            int2 cord = (int2)(idx, node_offset[j] + conf[j]);\n"
"            /*cord = select(cord, cord.yx, cord.x < cord.y); */\n"
"            g_score += read_imagef(reduce_energy, sampler, cord).x;\n"
"        }\n"
"        g_score += self_energy[idx];\n"
"    }\n"
"\n"
"    /* compute h */\n"
"    {\n"
"        for (int i = level+1; i < NUM_LEVEL; ++i) {\n"
"            float min_energy = FLT_MAX;\n"
"            for (int j = 0; j < rot_per_level[i]; ++j) {\n"
"                int idx = node_offset[i]+j;\n"
"                float cur_energy = self_energy[idx];\n"
"\n"
"                for (int k = 0; k <= level; ++k) {\n"
"                    int2 cord = (int2)(idx, node_offset[k] + conf[k]);\n"
"                    /*cord = select(cord, cord.yx, cord.x < cord.y);*/\n"
"                    cur_energy += read_imagef(reduce_energy, sampler, cord).x;\n"
"                }\n"
"\n"
"                cur_energy += read_imagef(pm_energy, sampler, (int2)(i+1, idx)).x;\n"
"                min_energy = min(min_energy, cur_energy);\n"
"            }\n"
"            h_score += min_energy;\n"
"        }\n"
"    }\n"
"\n"
"    return (float2)(g_score + h_score, g_score);\n"
"}\n"
"\n"
"__kernel\n"
"void initialize(\n"
"        __global heap_t *g_heap,\n"
"        __global int *g_heap_size,\n"
"        __global data_t *g_data,\n"
"        __global int *g_data_size,     /* [0 .. data_size-1] was filled */\n"
"\n"
"        __global uint *g_optimal,  /* this is a reinterperation of float */\n"
"\n"
"        const __global ushort *g_node_offset,\n"
"        const __global uchar  *g_rot_per_level,\n"
"        const __global float  *g_self_energy,\n"
"        __read_only image2d_t reduce_energy,\n"
"        __read_only image2d_t pm_energy)\n"
"{\n"
"    int id = get_global_id(0);\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"\\n\\n================initialize[%d]===================\\n\", id);\n"
"#endif\n"
"\n"
"    __local ushort node_offset[NUM_LEVEL];\n"
"    __local uchar  rot_per_level[NUM_LEVEL];\n"
"    __local float  self_energy[NUM_ROTAMER];\n"
"\n"
"    event_t event[3];\n"
"    event[0] = async_work_group_copy(node_offset,   g_node_offset,   NUM_LEVEL,   0);\n"
"    event[1] = async_work_group_copy(rot_per_level, g_rot_per_level, NUM_LEVEL,   0);\n"
"    event[2] = async_work_group_copy(self_energy,   g_self_energy,   NUM_ROTAMER, 0);\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"size of data_t: %d\\n\", sizeof(data_t));\n"
"#endif\n"
"\n"
"    int conf[NUM_LEVEL];\n"
"    float2 score;\n"
"\n"
"    conf[0] = id;\n"
"\n"
"    wait_group_events(3, event);\n"
"    score = __compute_score(conf,\n"
"                            0,\n"
"                            0.f,\n"
"                            node_offset,\n"
"                            rot_per_level,\n"
"                            self_energy,\n"
"                            reduce_energy,\n"
"                            pm_energy);\n"
"    g_heap[id*HEAP_SIZE + 1] = ((heap_t){ score.x, id });\n"
"    g_heap_size[id] = 1;\n"
"    atomic_min(g_optimal, flip_float(score.x));\n"
"\n"
"    g_data[id].f_score = score.x;\n"
"    g_data[id].g_score = score.y;\n"
"    g_data[id].level   = 0;\n"
"    g_data[id].conf[0] = id;\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    print_node(0, score.x, score.y, conf);\n"
"#endif\n"
"}\n"
"\n"
"__kernel __attribute__((reqd_work_group_size(GROUP_SIZE, 1, 1)))\n"
"void delete_min(\n"
"        __global heap_t *g_heap,\n"
"        __global int    *g_heap_size,\n"
"        __global data_t *g_data,\n"
"        __global int    *g_data_size,\n"
"        __global uint   *g_optimal,  /* uint is a reinterperation of float */\n"
"\n"
"        __global int    *g_father,\n"
"        __global int    *g_radix,\n"
"        __global int    *g_node_cnt,\n"
"        __global data_t *g_answer,\n"
"        __global int    *g_answer_size,\n"
"\n"
"        const __global uchar  *g_rot_per_level)\n"
"{\n"
"    __local uchar rot_per_level[NUM_LEVEL];\n"
"\n"
"    int id  = get_global_id(0);\n"
"    int lid = get_local_id(0);\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"\\n\\n================delete_min[%d]=================\\n\", id);\n"
"#endif\n"
"\n"
"    int heap_size = g_heap_size[id];\n"
"    bool in_work = (heap_size != 0);\n"
"\n"
"    /* =========================== delete_min ============================ */\n"
"    __global heap_t *heap = g_heap + id * HEAP_SIZE;\n"
"\n"
"    float node_score;\n"
"    int node_index = -1;\n"
"    int cur_level;\n"
"\n"
"    if (in_work) {\n"
"        node_score = heap[1].v;\n"
"        node_index = heap[1].idx;\n"
"        cur_level  = g_data[node_index].level + 1;\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"        printf(PR\"pop node: %d:%.3f\\n\", node_index, heap[1].v);\n"
"#endif\n"
"\n"
"        heap_t now_val = heap[heap_size--];\n"
"        g_heap_size[id] = heap_size;\n"
"\n"
"        /* pop from heap */\n"
"        int now = 1;\n"
"        int next;\n"
"        while ((next = now*2) <= heap_size) {\n"
"            heap_t next_val = heap[next];\n"
"            heap_t next_val2 = heap[next+1];\n"
"            bool inc = (next+1 <= heap_size) && (next_val2.v < next_val.v);\n"
"            if (inc) {\n"
"                next += 1;\n"
"                next_val = next_val2;\n"
"            }\n"
"\n"
"            if (next_val.v < now_val.v) {\n"
"                heap[now] = next_val;\n"
"                now = next;\n"
"            } else\n"
"                break;\n"
"        }\n"
"        heap[now] = now_val;\n"
"    }\n"
"\n"
"    __local int  l_radix[NUM_LEVEL];\n"
"    __local uint l_optimal;\n"
"    l_optimal = UINT_MAX;\n"
"    if (lid < NUM_LEVEL) {\n"
"        l_radix[lid] = 0;\n"
"        rot_per_level[lid] = g_rot_per_level[lid];\n"
"    }\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"    /* =========================== update answer ============================ */\n"
"    if (in_work) {\n"
"        if (cur_level == NUM_LEVEL) {\n"
"            /* add to answer array if we are leaves */\n"
"            int answer_index = atomic_inc(g_answer_size);\n"
"            g_answer[answer_index] = g_data[node_index];\n"
"            if (heap_size > 0)\n"
"                atomic_min(&l_optimal, flip_float(heap[1].v));\n"
"\n"
"            in_work = false;\n"
"            node_index = -1;\n"
"        } else\n"
"            atomic_min(&l_optimal, flip_float(node_score));\n"
"    }\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    if (lid == 0) {\n"
"        atomic_min(g_optimal, l_optimal);\n"
"    }\n"
"    /* ======================== radix sort first part ======================== */\n"
"    g_father[id] = node_index;\n"
"\n"
"    if (in_work)\n"
"        atomic_add(&l_radix[cur_level], rot_per_level[cur_level]);\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    if (lid < NUM_LEVEL) {\n"
"        g_node_cnt[id] = l_radix[lid];\n"
"    }\n"
"\n"
"#pragma unroll\n"
"    for (int i = 1; i < NUM_LEVEL; i *= 2) {\n"
"        if (lid - i >= 0 && lid < NUM_LEVEL)\n"
"            l_radix[lid] += l_radix[lid - i];\n"
"        barrier(CLK_LOCAL_MEM_FENCE);\n"
"    }\n"
"    if (lid < NUM_LEVEL) {\n"
"        atomic_add(&g_radix[lid], l_radix[lid]);\n"
"    }\n"
"}\n"
"\n"
"__kernel __attribute__((reqd_work_group_size(GROUP_SIZE, 1, 1)))\n"
"void radix_sort(__global int  *g_father,\n"
"                __global int2 *g_input,\n"
"                __global int  *g_radix,\n"
"                __global int  *g_node_cnt,\n"
"\n"
"                __global data_t *g_data,\n"
"\n"
"                const __global uchar *g_rot_per_level)\n"
"{\n"
"    __local uchar rot_per_level[NUM_LEVEL];\n"
"    __local int   l_radix[NUM_LEVEL];\n"
"\n"
"    int id  = get_global_id(0);\n"
"    int lid = get_local_id(0);\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"\\n\\n================radix_sort[%d]=================\\n\", id);\n"
"#endif\n"
"\n"
"    if (lid < NUM_LEVEL) {\n"
"        rot_per_level[lid] = g_rot_per_level[lid];\n"
"\n"
"        int t = g_node_cnt[id];\n"
"        l_radix[lid] = atomic_sub(&g_radix[lid], t) - t;\n"
"    }\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"    int node_index = g_father[id];\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"node_index: %d\\n\", node_index);\n"
"#endif\n"
"    if (node_index >= 0) {\n"
"        int cur_level = g_data[node_index].level + 1;\n"
"        int index = atomic_add(&l_radix[cur_level], rot_per_level[cur_level]);\n"
"#ifdef VERBOSE_DEBUG\n"
"        printf(PR\"cur_level: %d\\n\", cur_level);\n"
"#endif\n"
"        for (int i = 0; i < rot_per_level[cur_level]; ++i) {\n"
"            g_input[index++] = (int2)(node_index, i);\n"
"#ifdef VERBOSE_DEBUG\n"
"            printf(PR\"g_input[%d] = (index: %d, conf: %d)\\n\", index-1, node_index, i);\n"
"#endif\n"
"        }\n"
"    }\n"
"}\n"
"\n"
"\n"
"\n"
"__kernel __attribute__((reqd_work_group_size(GROUP_SIZE, 1, 1)))\n"
"void compute_score(\n"
"        __global int2   *g_input,\n"
"        __global data_t *g_data,\n"
"        __global int    *g_data_size,\n"
"        __global int    *g_output_size,\n"
"\n"
"        const __global ushort *g_node_offset,\n"
"        const __global uchar  *g_rot_per_level,\n"
"        const __global float  *g_self_energy,\n"
"        __read_only image2d_t  reduce_energy,\n"
"        __read_only image2d_t  pm_energy)\n"
"{\n"
"    __local uchar  rot_per_level[NUM_LEVEL];\n"
"    __local ushort node_offset[NUM_LEVEL];\n"
"    __local float  self_energy[NUM_ROTAMER];\n"
"\n"
"    event_t event[3];\n"
"    event[0] = async_work_group_copy(rot_per_level, g_rot_per_level, NUM_LEVEL,   0);\n"
"    event[1] = async_work_group_copy(node_offset,   g_node_offset,   NUM_LEVEL,   0);\n"
"    event[2] = async_work_group_copy(self_energy,   g_self_energy,   NUM_ROTAMER, 0);\n"
"\n"
"    int id = get_global_id(0);\n"
"    int global_size = get_global_size(0);\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"\\n\\n================compute_score[%d]=================\\n\", id);\n"
"#endif\n"
"\n"
"    int index  = *g_data_size + id;\n"
"\n"
"    if (index >= *g_output_size)\n"
"        return;\n"
"\n"
"    int2   input;\n"
"    int    level;\n"
"    float  oscore;\n"
"    float2 score;\n"
"    int    conf[NUM_LEVEL];\n"
"    \n"
"    input  = g_input[id];\n"
"    level  = g_data[input.x].level + 1;\n"
"    oscore = g_data[input.x].g_score;\n"
"#pragma unroll\n"
"    for (int i = 0; i < NUM_LEVEL; ++i)\n"
"        conf[i] = g_data[input.x].conf[i];\n"
"    conf[level] = input.y;\n"
"\n"
"    wait_group_events(3, event);\n"
"    score = __compute_score(conf,\n"
"                            level,\n"
"                            oscore,\n"
"                            node_offset,\n"
"                            rot_per_level,\n"
"                            self_energy,\n"
"                            reduce_energy,\n"
"                            pm_energy);\n"
"\n"
"    g_data[index].f_score = score.x;\n"
"    g_data[index].g_score = score.y;\n"
"    g_data[index].level   = level;\n"
"#pragma unroll\n"
"    for (int i = 0; i < NUM_LEVEL; ++i)\n"
"        g_data[index].conf[i] = conf[i];\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"index[%d]: \", index);\n"
"    print_node(level, score.x, score.y, conf);\n"
"#endif\n"
"}\n"
"\n"
"\n"
"__kernel __attribute__((reqd_work_group_size(GROUP_SIZE, 1, 1)))\n"
"void push_back(\n"
"        __global heap_t *g_heap,\n"
"        __global int *g_heap_size,\n"
"        __global data_t *g_data,\n"
"        __global int *g_data_size,\n"
"        __global int *g_output_size,\n"
"        __global int *begin_index,\n"
"        __global int *begin_index2,\n"
"        __global uint *g_optimal)\n"
"{\n"
"    int global_size = get_global_size(0);\n"
"    int id  = get_global_id(0);\n"
"    int lid = get_local_id(0);\n"
"\n"
"#ifdef VERBOSE_DEBUG\n"
"    printf(PR\"\\n\\n================push_back[%d]=================\\n\", id);\n"
"    if (id == 0)\n"
"        printf(PR\"g_data_size: %d g:output: %d begin: %d\\n\",\n"
"               *g_data_size, *g_output_size, *begin_index);\n"
"#endif\n"
"\n"
"    int data_size = *g_data_size;\n"
"    int output_size = *g_output_size;\n"
"\n"
"    int index = id - *begin_index;\n"
"    index = select(index, index + global_size, index < 0);\n"
"    index += data_size;\n"
"\n"
"    __global heap_t *heap = g_heap + HEAP_SIZE*id;\n"
"    int heap_size = g_heap_size[id];\n"
"\n"
"    uint optimal = -1;\n"
"    while (index < output_size) {\n"
"        heap_t val = { g_data[index].f_score, index };\n"
"        optimal = min(optimal, flip_float(val.v));\n"
"#ifdef VERBOSE_DEBUG\n"
"        printf(PR\"assign node [%d] to this heap\\n\", index);\n"
"#endif\n"
"\n"
"        int now = ++heap_size;\n"
"        while (now > 1) {\n"
"            int next = now / 2;\n"
"            heap_t next_val = heap[next];\n"
"            if (val.v < next_val.v) {\n"
"                heap[now] = next_val;\n"
"                now = next;\n"
"            } else\n"
"                break;\n"
"        }\n"
"        heap[now] = val;\n"
"\n"
"        index += global_size;\n"
"    }\n"
"    g_heap_size[id] = heap_size;\n"
"    if (index == output_size)\n"
"        *begin_index2 = id;\n"
"\n"
"    __local uint l_optimal;\n"
"    l_optimal = -1;\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    atomic_min(&l_optimal, optimal);\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"    if (lid == 0)\n"
"        atomic_min(g_optimal, l_optimal);\n"
"}\n"
"";
#include "ccore/c_target.h"
#include "cbase/c_allocator.h"
#include "cbase/c_integer.h"

#include "cjobs/c_queue.h"
#include "cjobs/c_job.h"
#include "cjobs/private/c_signal.h"

#include <atomic>
#include <thread>

#ifdef TARGET_PC
#    include <windows.h>
#endif

#ifdef TARGET_MAC
#    include <mach/mach.h>
#    include <mach/task.h>
#endif

namespace ncore
{
    namespace njob
    {
        struct node_t
        {
            job_t*  m_job;
            node_t* m_next;
        };

        struct group_t
        {
            const char* m_name;
        };

        struct graph_t
        {
            u16*     m_job_index_array; // This array will be kept sorted, where the sort key is the job in job_array
            node_t** m_job_array;
            node_t*  m_nodes;
            group_t* m_groups;
            s32      m_num_jobs;
            s32      m_num_nodes;
            s32      m_num_groups;
            s32      m_max_jobs;
            s32      m_max_nodes;
            s32      m_max_groups;
        };

        graph_t* g_createGraph(alloc_t* allocator, system_t* system, s32 maxJobs, s32 maxDependencies, s32 maxGroups)
        {
            graph_t* graph = (graph_t*)allocator->allocate(sizeof(graph_t));
            if (graph == nullptr)
                return nullptr;

            graph->m_num_jobs   = 0;
            graph->m_num_nodes  = 0;
            graph->m_num_groups = 0;
            graph->m_max_jobs   = maxJobs;
            graph->m_max_nodes  = maxJobs + maxDependencies;
            graph->m_max_groups = maxGroups;

            graph->m_job_index_array = (u16*)allocator->allocate(sizeof(u16) * maxJobs);
            graph->m_job_array       = (node_t**)allocator->allocate(sizeof(node_t*) * maxJobs);
            graph->m_nodes           = (node_t*)allocator->allocate(sizeof(node_t) * graph->m_max_nodes);
            graph->m_groups          = (group_t*)allocator->allocate(sizeof(group_t) * maxGroups);

            return graph;
        }

        void g_destroyGraph(alloc_t* allocator, graph_t*& graph)
        {
            if (graph == nullptr)
                return;

            allocator->deallocate(graph->m_job_index_array);
            allocator->deallocate(graph->m_job_array);
            allocator->deallocate(graph->m_nodes);
            allocator->deallocate(graph);
            graph = nullptr;
        }

        void graph_reset(graph_t* graph) {}
        s32  graph_add_group(graph_t* graph, const char* name) { return 0; }
        void graph_add_job(graph_t* graph, job_t* job, s32 group) {}
        bool graph_add_dep(graph_t* graph, job_t* first, job_t* then) { return false; }
        void graph_execute(graph_t* graph) {}
        s32  graph_job_finished(graph_t* graph, job_t* job) { return 0; }

    } // namespace njob
} // namespace ncore

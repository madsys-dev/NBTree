#include "config.h"
#include "util.h"
#include "timer.h"
#include "benchmarks.h"

#include <fstream>
#include <thread>
#include <boost/thread/barrier.hpp>
#include <unistd.h>
#include <sys/mman.h>

// #define PERF_LATENCY
#ifdef NBTREE_WR
#include "nbtree_wr.h"
#else
#ifdef NBTREE_W
#include "nbtree_w.h"
#else
#include "nbtree.h"
#endif
#endif

char *thread_space_start_addr;
__thread char *start_addr;
__thread char *curr_addr;
uint64_t allocate_size = 113ULL * 1024ULL * 1024ULL * 1024ULL;

char *thread_mem_start_addr;
__thread char *start_mem;
__thread char *curr_mem;
uint64_t allocate_mem = 113ULL * 1024ULL * 1024ULL * 1024ULL;

using namespace std;

class Coordinator
{
	class Result
	{
	public:
		uint64_t throughput;
		int lat[100000];

		Result()
		{
			throughput = 0;
			for (int i = 0; i < 100000; ++i)
			{
				lat[i] = 0;
			}
		}

		void operator+=(Result &r)
		{
			this->throughput += r.throughput;
		}

		void operator/=(double r)
		{
			this->throughput /= r;
		}

		void add_latency(Result &r)
		{
			for (int i = 0; i < 100000; ++i)
			{
				lat[i] += r.lat[i];
			}
		}
	};

public:
	Coordinator(Config _conf) : conf(_conf)
	{
	}
	int stick_this_thread_to_core(int core_id)
	{
		int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
		if (core_id < 0 || core_id >= num_cores)
			return EINVAL;

		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(core_id, &cpuset);

		pthread_t current_thread = pthread_self();
		return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
	}

	void print_taillatency(int *record, unsigned long tx, const char *op = NULL)
	{
		uint64_t cnt = 0;
		uint64_t nb_50 = tx / 2;
		uint64_t nb_90 = tx * 0.9;
		uint64_t nb_99 = tx * 0.99;
		uint64_t nb_999 = tx * 0.999;

		bool flag_50 = false, flag_90 = false, flag_99 = false, flag_999 = false;
		double latency_50, latency_90, latency_99, latency_999;

		for (int i = 0; i < 100000 && !(flag_50 && flag_90 && flag_99 && flag_999); i++)
		{
			cnt += record[i];
			if (!flag_50 && cnt >= nb_50)
			{
				latency_50 = (double)i / 100.0;
				flag_50 = true;
			}
			if (!flag_90 && cnt >= nb_90)
			{
				latency_90 = (double)i / 100.0;
				flag_90 = true;
			}
			if (!flag_99 && cnt >= nb_99)
			{
				latency_99 = (double)i / 100.0;
				flag_99 = true;
			}
			if (!flag_999 && cnt >= nb_999)
			{
				latency_999 = (double)i / 100.0;
				flag_999 = true;
			}
		}
		printf("count:%lu\n", tx);
		printf("%s medium latency is %.1lfus\n", op, latency_50);
		printf("%s 90%% latency is %.1lfus\n", op, latency_90);
		printf("%s 99.0%% latency is %.1lfus\n", op, latency_99);
		printf("%s 99.9%% latency is %.1lfus\n", op, latency_999);
	}
	void worker(btree *tree, int workerid, Result *result, Benchmark *b)
	{
		long long lat;
		nsTimer clk;
		start_addr = thread_space_start_addr + workerid * SPACE_PER_THREAD;
		curr_addr = start_addr;
		start_mem = thread_mem_start_addr + workerid * MEM_PER_THREAD;
		curr_mem = start_mem;

		Benchmark *benchmark = getBenchmark(conf, workerid);
		if (conf.benchmark == INSERT_ONLY || conf.benchmark == UPSERT)
		{
			memset(start_addr, 0, SPACE_PER_THREAD);
			clear_cache();
		}

		stick_this_thread_to_core(workerid * 2 + 1);
		printf("[WORKER]\thello, I am worker %d\n", workerid);
		bar->wait();

		while (done == 0)
		{
			volatile auto next_operation = benchmark->nextOperation();
			OperationType op = next_operation.first;
			long long d = next_operation.second;
		#ifdef PERF_LATENCY
			clk.start();
		#endif
			switch (op)
			{
			case INSERT:
				tree->insert(d, (char *)(d));
				break;
			case REMOVE:
				tree->remove(d);
				break;
			case UPDATE:
				tree->update(d, (char *)(d + result->throughput + 1));
				break;
			case GET:
				tree->search(d);
				break;
			default:
				printf("not support such operation: %d\n", op);
				exit(-1);
			}
		#ifdef PERF_LATENCY
			lat = clk.end();
			result->lat[lat/10] +=1;	
		#endif
			result->throughput++;
		}
	}

	void run()
	{
		// Create memory pool
		int fd = open("/mnt/pmem1/btree", O_RDWR);
		if (fd < 0)
		{
			printf("[NVM MGR]\tfailed to open nvm file\n");
			exit(-1);
		}
		if (ftruncate(fd, allocate_size) < 0)
		{
			printf("[NVM MGR]\tfailed to truncate file\n");
			exit(-1);
		}
		void *pmem = mmap(NULL, allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		memset(pmem, 0, SPACE_OF_MAIN_THREAD);
		start_addr = (char *)pmem;
		curr_addr = start_addr;
		thread_space_start_addr = (char *)pmem + SPACE_OF_MAIN_THREAD;
		void *mem = new char[allocate_mem];
		start_mem = (char *)mem;
		curr_mem = start_mem;
		thread_mem_start_addr = (char *)mem + MEM_OF_MAIN_THREAD;
		
		// Warm-up
		printf("[COORDINATOR]\tWarm-up..\n");
		btree *tree = new btree();
		Benchmark *benchmark = getBenchmark(conf);
		nsTimer init, runtime;
		init.start();
		for (unsigned long i = 0; i < conf.init_keys; i++)
		{
			uint64_t key = benchmark->nextInitKey();
			tree->insert(key, (char *)key);
		}
		init.end();
		clear_cache();
		printf("warm-up time:%.3f ms\n", init.duration() / 1000000.0);
		printf("average insert time:%.3f us\n", init.duration() / conf.init_keys / 1000.0);

		// Start benchmark
		printf("[COORDINATOR]\tStart benchmark..\n");
		Result *results = new Result[conf.num_threads];
		memset(results, 0, sizeof(Result) * conf.num_threads);
		std::thread **pid = new std::thread *[conf.num_threads];
		bar = new boost::barrier(conf.num_threads + 1);
		for (int i = 0; i < conf.num_threads; i++)
		{
			pid[i] = new std::thread(&Coordinator::worker, this, tree, i, &results[i], benchmark);
		}
		bar->wait();
		runtime.start();
		usleep(conf.duration * 1000000);
		done = 1;

		// Get results
		Result final_result;
		for (int i = 0; i < conf.num_threads; i++)
		{
			pid[i]->join();
			final_result += results[i];
			printf("[WORKER]\tworker %d result %ld\n", i, results[i].throughput);
		}
		runtime.end();
		printf("runtime:%.3f ms\n", (double)runtime.duration() / 1000000);
		printf("[COORDINATOR]\tFinish benchmark..\n");
		printf("[COORDINATOR]\ttotal throughput: %.3lf Mtps\n", (double)final_result.throughput / 1000000.0 / conf.duration);
#ifdef PERF_LATENCY
		for (int i = 0; i < conf.num_threads; i++)
		{
			final_result.add_latency(results[i]);
		}
		print_taillatency(final_result.lat, final_result.throughput, "total");
#endif

		delete tree;
		delete[] pid;
		delete[] results;
	}

private:
	Config conf __attribute__((aligned(64)));
	volatile int done __attribute__((aligned(64))) = 0;
	boost::barrier *bar __attribute__((aligned(64))) = 0;
};

int main(int argc, char **argv)
{
	Config conf;
	parse_arguments(argc, argv, conf);

	Coordinator coordinator(conf);
	coordinator.run();
}

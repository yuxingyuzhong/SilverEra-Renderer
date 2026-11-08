#include "../局部命名空间使用.h"
//获取日志系统（线程创建失败告警）
#include "src/render/Renderer/core/结果检查.h"

//游戏引擎命名空间
namespace engine
{
	//析构（回收全部工作线程）
	Thread_Pool::~Thread_Pool(void)
	{
		threads_stop();
	}

	/*
	工作线程主循环（第一百七十七轮）
	每轮先等「任务代号变化」或「停止标志」；醒来后领取并执行本轮块，收尾时递减未完成线程数，
	最后一个完成者唤醒等待中的调用线程。停止标志只在无在途任务时设置（见 threads_stop）。
	*/
	void Thread_Pool::worker_main(void)
	{
		//本线程已处理到的任务代号（启动时为 0，与初始代号一致）
		uint64_t seen_generation = 0;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [this, &seen_generation]
				{
					return stopping || job_generation != seen_generation;
				});
				//若收到停止信号则退出主循环
				if (stopping)
					return;
				//记录本次任务代号（同代号不重复执行）
				seen_generation = job_generation;
			}
			//执行本轮全部块（与其他线程竞争领取）
			chunks_run();
			{
				std::lock_guard<std::mutex> lock(mutex);
				//本线程本轮收尾
				if (pending_workers > 0)
					--pending_workers;
				//若全部工作线程均已收尾则唤醒调用线程
				if (pending_workers == 0)
					done.notify_all();
			}
		}
	}

	//当前任务的块领取与执行（调用线程与工作线程共用）
	void Thread_Pool::chunks_run(void)
	{
		for (;;)
		{
			//原子领取下一块的起始下标（领取越界即表示本轮已无剩余块）
			const std::size_t begin = job_cursor.fetch_add(job_chunk, std::memory_order_relaxed);
			//若已无剩余块
			if (begin >= job_total)
				return;
			//本块结束下标（末块截到总数）
			const std::size_t end = (begin + job_chunk < job_total) ? begin + job_chunk : job_total;
			//执行本块（只写本区间对应槽位）
			(*job_body)(begin, end);
		}
	}

	//启动工作线程
	bool Thread_Pool::threads_start(uint32_t count)
	{
		//已有线程则先全部回收
		threads_stop();
		//线程数钳到上限
		if (count > 64)
			count = 64;
		//零线程即关闭
		if (count == 0)
			return true;
		{
			std::lock_guard<std::mutex> lock(mutex);
			//复位停止标志与任务代号
			stopping = false;
			job_generation = 0;
		}
		/*
		逐线程创建
		std::thread 构造没有 nothrow 形式，系统资源不足时会抛 std::system_error，
		故此处仅捕获该异常并回收已建线程（不做任何内存分配相关的异常处理）。
		*/
		try
		{
			for (uint32_t index = 0; index < count; ++index)
				workers.emplace_back(&Thread_Pool::worker_main, this);
		}
		catch (const std::system_error& error)
		{
			logger.warn("线程池:: 工作线程创建失败（已建 {} 个）：{}", workers.size(), error.what());
			threads_stop();
			return false;
		}
		return true;
	}

	//停止并回收全部工作线程
	void Thread_Pool::threads_stop(void)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			//若无工作线程则无事可做
			if (workers.empty())
				return;
			//置停止标志并唤醒全部工作线程
			stopping = true;
		}
		wake.notify_all();
		for (std::thread& worker : workers)
			//若线程仍可加入则等待其退出
			if (worker.joinable())
				worker.join();
		workers.clear();
		{
			std::lock_guard<std::mutex> lock(mutex);
			//复位任务状态（停止后不再有在途任务）
			stopping = false;
			job_body = nullptr;
			pending_workers = 0;
		}
	}

	//当前工作线程数
	uint32_t Thread_Pool::thread_count_get(void) const
	{
		return static_cast<uint32_t>(workers.size());
	}

	//是否已有工作线程
	bool Thread_Pool::active(void) const
	{
		return !workers.empty();
	}

	/*
	分段并行执行
	块长按「线程数 × 4」推算（保证负载均衡与末块利用率），并以 min_chunk 为下限；
	元素过少时退化为主调线程整段执行（避免线程唤醒开销超过收益）。
	*/
	bool Thread_Pool::ranges_run(std::size_t total, std::size_t min_chunk,
		const std::function<void(std::size_t, std::size_t)>& body)
	{
		//空区间直接成功（无需派发）
		if (total == 0)
			return true;
		//块长下限至少为一
		if (min_chunk == 0)
			min_chunk = 1;
		//当前工作线程数
		const std::size_t worker_count = workers.size();
		//无工作线程或元素过少：调用线程整段执行
		if (worker_count == 0 || total < min_chunk * 2)
		{
			body(0, total);
			return true;
		}
		//目标块长（线程数 × 4 块）
		std::size_t chunk = total / (worker_count * 4);
		if (chunk < min_chunk)
			chunk = min_chunk;
		//块长过大时压到「总数的一半」，保证至少两块
		if (chunk * 2 > total)
			chunk = total / 2;
		{
			std::lock_guard<std::mutex> lock(mutex);
			//登记本轮任务
			job_body = &body;
			job_total = total;
			job_chunk = chunk;
			job_cursor.store(0, std::memory_order_relaxed);
			pending_workers = worker_count;
			//任务代号自增（工作线程据此醒来）
			++job_generation;
			++dispatch_count;
		}
		//唤醒全部工作线程
		wake.notify_all();
		//调用线程同时参与执行（不空等）
		chunks_run();
		{
			std::unique_lock<std::mutex> lock(mutex);
			//等待全部工作线程收尾
			done.wait(lock, [this] { return pending_workers == 0; });
			//清空任务登记（此后不再有线程读取 body）
			job_body = nullptr;
		}
		return true;
	}

	//累计并行派发次数
	uint64_t Thread_Pool::dispatch_count_get(void) const
	{
		return dispatch_count;
	}
}
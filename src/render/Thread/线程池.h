#pragma once
//预编译头
#include "Engine/EngineRenderer/common/前置头文件包含.h"

//游戏引擎命名空间
namespace engine
{
	/*
	帧内粗粒度并行执行器（第一百七十七轮）
	用途：把「逐元素只写自己槽位」的纯 CPU 循环（实例级视锥剔除、实例缓冲差异扫描）按块分派到工作线程，
	主线程同时参与执行，返回时全部块必已完成；块之间无共享写入，故结果与单线程逐字节一致。
	口径：
		① 默认线程数为 0（不启动任何工作线程），此时 ranges_run 退化为「调用线程整段执行」，
		   行为与引入本类前逐字节一致；
		② 单个任务为同步调用（调用方阻塞到全部块完成），故不存在跨帧残留任务；
		③ 块长由 ranges_run 按「线程数 × 4」推算并受 min_chunk 下限约束，块划分只影响耗时、不影响结果。
	*/
	class Thread_Pool
	{
	public:
		Thread_Pool(void) = default;
		//析构时回收全部工作线程
		~Thread_Pool(void);
		//禁止拷贝（持有线程与同步原语）
		Thread_Pool(const Thread_Pool&) = delete;
		Thread_Pool& operator=(const Thread_Pool&) = delete;
		/*
		启动工作线程（count 钳到 0..64；0 = 仅关闭）
		已启动时先停止旧线程再按新数量启动；线程创建失败时回收已建线程并返回假。
		*/
		bool threads_start(uint32_t count);
		//停止并回收全部工作线程（无在途任务时可安全调用；幂等）
		void threads_stop(void);
		//当前工作线程数（0 = 关闭）
		uint32_t thread_count_get(void) const;
		//是否已有工作线程
		bool active(void) const;
		/*
		分段并行执行
		total 为元素总数，body 以半开区间 [begin, end) 被调用若干次（各区间互不重叠且拼齐 [0, total)）。
		未启动线程或元素过少（total < 2 × min_chunk）时由调用线程整段执行。
		body 只允许写各自区间对应的槽位，不得跨区间写入。
		*/
		bool ranges_run(std::size_t total, std::size_t min_chunk,
			const std::function<void(std::size_t, std::size_t)>& body);
		//累计派发的并行任务次数（观测；单线程退化的任务不计）
		uint64_t dispatch_count_get(void) const;

	private:
		//工作线程主循环
		void worker_main(void);
		//当前任务的块领取与执行（调用线程与工作线程共用）
		void chunks_run(void);
		//同步原语与线程集合
		std::mutex mutex;
		std::condition_variable wake;
		std::condition_variable done;
		std::vector<std::thread> workers;
		//当前任务（job_body 指向调用方栈上的可调用体，任务收束后清空）
		const std::function<void(std::size_t, std::size_t)>* job_body = nullptr;
		std::size_t job_total = 0;
		std::size_t job_chunk = 1;
		std::atomic<std::size_t> job_cursor{ 0 };
		//本轮尚未收尾的工作线程数
		std::size_t pending_workers = 0;
		//任务代号（每派发一次自增；工作线程据此醒来）
		uint64_t job_generation = 0;
		//停止标志
		bool stopping = false;
		//累计并行派发次数
		uint64_t dispatch_count = 0;
	};
}

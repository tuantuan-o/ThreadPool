#include"threadpool.h"
#include<functional>
#include<thread>
#include<iostream>
#include<mutex>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDEL_TIME = 10; //单位：秒

// 线程池构造
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreadHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// 线程池的析构
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	notEmpty_.notify_all();

	// 等待线程池里面所有的线程返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) {
		return;
	}
	poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) {
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}
// 设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState()) {
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreadHold_ = threshhold;
	}
}

// 给线程池提交任务   用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {

	// 首先获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// 线程的通信，等待任务队列有空余
	// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	if (!notFull_.wait_for(lock
		, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; })) {
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return task->getResult();不行 因为线程执行完task，task对象就被析构掉了
		return Result(std::move(sp),false); // Task  Result
	}
	//如果有空余，就放入任务队列
	taskQue_.emplace(sp);
	taskSize_++;
	// 因为新放了任务，任务队列肯定不空了，在notEmpty_上进行通知，赶快分配线程执行任务
	notEmpty_.notify_all();

	// cached模式 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHED //cached模式
		&& taskSize_ > idleThreadSize_ //任务数量大于空闲线程数量
		&& curThreadSize_ < threadSizeThreadHold_) { //当前线程数量小于系统可支持的最大线程数量

		std::cout << ">>> create new thread: " << std::endl;

		// 创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start();
		// 修改线程个数相关的变量
		curThreadSize_++;
		idleThreadSize_++;
		//threads_.emplace(std::move(ptr));
	}

	// 返回任务的Result对象
	return Result(std::move(sp));
	// return task->getResult();不行
}

//开启线程池
void ThreadPool::start(int initThreadSize) {

	//设置线程池的运行状态
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++) {
		// 创建thread线程对象的时候，把线程函数给到thread线程对象
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// 启动所以线程 std::vector<Thread*> threads_; 
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();  // 需要去执行一个线程函数
		idleThreadSize_++; //记录初始空闲线程的数量
	}
}

//定义线程函数  线程池的所有线程消费任务队列里边的任务
void ThreadPool::threadFunc(int threadid) {  //线程函数返回，相应的线程也就结束了
	auto lastTime = std::chrono::high_resolution_clock::now();
	while(isPoolRunning_) {
		std::shared_ptr<Task> task;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "threadid: " << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;

			// 每一秒钟返回一次  怎么区分超时返回，还是有任务待执行返回
			while (taskQue_.size() == 0) {
				// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程
				// 结束(超过初始initThreadSize_数量的线程)回收掉
				if (poolMode_ == PoolMode::MODE_CACHED) {
					// 条件变量，超时返回了
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock::now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDEL_TIME
							&& curThreadSize_ > initThreadSize_) {
							// 开始回收当前线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象从线程列表容器中删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
							return;
						}
					}
				}
				else {
					// 默认的fixed模式
						// 等待notEmpty条件
					notEmpty_.wait(lock);
				}

				//线程池要结束，回收线程资源
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					exitCond_.notify_all();
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
					return; //结束线程函数，就是结束当前线程了
				}
			}
			idleThreadSize_--;
			std::cout << "tid: " << std::this_thread::get_id()
				<<"获取任务成功..." << std::endl;

			// 从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			// 取出一个任务，得进行通知，通知可以继续提交生产任务了
			notFull_.notify_all();
		}
		// 当前线程负责执行这个任务
		if (task != nullptr) {
			//task->run();  // 执行任务，把任务的返回值通过setVal方法给到Result
			task->exec();
		}
		idleThreadSize_++;
		auto lastTime = std::chrono::high_resolution_clock::now();// 更新线程执行完任务的时间
	}
	threads_.erase(threadid);
	exitCond_.notify_all();
	std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
}
bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}



//////////////////    线程方法实现
int Thread::generateId_ = 0;

// 线程构造
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{

}
// 线程析构
Thread::~Thread() {

}

//启动线程
void Thread::start() {

	// 创建一个线程来执行一个线程函数
	std::thread t(func_,threadId_); //创建线程对象的同时，启动线程
	// C++11来说，线程对象t 和线程函数func_
	// 出了这个局部作用域，线程对象就析构了，但是线程函数还在，所以需要把线程对象设置为分离线程
	t.detach();

}

int Thread::getId() const {
	return threadId_;
}

////////////////// Task方法实现
Task::Task()
	:result_(nullptr)
{}

void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run()); // 这里发生多态调用
	}
}

void Task::setResult(Result* res) {
	result_ = res;
}

//Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isvalid)
	:task_(task)
	,isValid_(isvalid)
{
	task->setResult(this);
}

Any Result::get() {  // 用户调用的
	if (!isValid_) {
		return "";
	}
	sem_.wait(); //task任务如果没有执行完，这里会阻塞用户的线程
	//如果任务执行完了，sem_会post一下，信号量就有资源了
	//这里就可以消耗信号量的一个资源，返回带右值引用的any_
	return std::move(any_);
}

void Result::setVal(Any any) { 
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); //已经获取了任务的返回值，增加信号量资源
}

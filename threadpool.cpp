#include"threadpool.h"
#include<functional>
#include<thread>
#include<iostream>
#include<mutex>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDEL_TIME = 10; //��λ����

// �̳߳ع���
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

// �̳߳ص�����
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	notEmpty_.notify_all();

	// �ȴ��̳߳��������е��̷߳���
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) {
		return;
	}
	poolMode_ = mode;
}

// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) {
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}
// �����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState()) {
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreadHold_ = threshhold;
	}
}

// ���̳߳��ύ����   �û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {

	// ���Ȼ�ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// �̵߳�ͨ�ţ��ȴ���������п���
	// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock
		, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; })) {
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return task->getResult();���� ��Ϊ�߳�ִ����task��task����ͱ���������
		return Result(std::move(sp),false); // Task  Result
	}
	//����п��࣬�ͷ����������
	taskQue_.emplace(sp);
	taskSize_++;
	// ��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	// cachedģʽ ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED //cachedģʽ
		&& taskSize_ > idleThreadSize_ //�����������ڿ����߳�����
		&& curThreadSize_ < threadSizeThreadHold_) { //��ǰ�߳�����С��ϵͳ��֧�ֵ�����߳�����

		std::cout << ">>> create new thread: " << std::endl;

		// �������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// �����߳�
		threads_[threadId]->start();
		// �޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++;
		//threads_.emplace(std::move(ptr));
	}

	// ���������Result����
	return Result(std::move(sp));
	// return task->getResult();����
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) {

	//�����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++) {
		// ����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		//threads_.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// ���������߳� std::vector<Thread*> threads_; 
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();  // ��Ҫȥִ��һ���̺߳���
		idleThreadSize_++; //��¼��ʼ�����̵߳�����
	}
}

//�����̺߳���  �̳߳ص������߳��������������ߵ�����
void ThreadPool::threadFunc(int threadid) {  //�̺߳������أ���Ӧ���߳�Ҳ�ͽ�����
	auto lastTime = std::chrono::high_resolution_clock::now();
	while(isPoolRunning_) {
		std::shared_ptr<Task> task;
		{
			// �Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "threadid: " << std::this_thread::get_id()
				<< "���Ի�ȡ����..." << std::endl;

			// ÿһ���ӷ���һ��  ��ô���ֳ�ʱ���أ������������ִ�з���
			while (taskQue_.size() == 0) {
				// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳�
				// ����(������ʼinitThreadSize_�������߳�)���յ�
				if (poolMode_ == PoolMode::MODE_CACHED) {
					// ������������ʱ������
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock::now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDEL_TIME
							&& curThreadSize_ > initThreadSize_) {
							// ��ʼ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
							return;
						}
					}
				}
				else {
					// Ĭ�ϵ�fixedģʽ
						// �ȴ�notEmpty����
					notEmpty_.wait(lock);
				}

				//�̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					exitCond_.notify_all();
					std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
					return; //�����̺߳��������ǽ�����ǰ�߳���
				}
			}
			idleThreadSize_--;
			std::cout << "tid: " << std::this_thread::get_id()
				<<"��ȡ����ɹ�..." << std::endl;

			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			// ȡ��һ�����񣬵ý���֪ͨ��֪ͨ���Լ����ύ����������
			notFull_.notify_all();
		}
		// ��ǰ�̸߳���ִ���������
		if (task != nullptr) {
			//task->run();  // ִ�����񣬰�����ķ���ֵͨ��setVal��������Result
			task->exec();
		}
		idleThreadSize_++;
		auto lastTime = std::chrono::high_resolution_clock::now();// �����߳�ִ���������ʱ��
	}
	threads_.erase(threadid);
	exitCond_.notify_all();
	std::cout << "threadid: " << std::this_thread::get_id() << " exit!";
}
bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}



//////////////////    �̷߳���ʵ��
int Thread::generateId_ = 0;

// �̹߳���
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{

}
// �߳�����
Thread::~Thread() {

}

//�����߳�
void Thread::start() {

	// ����һ���߳���ִ��һ���̺߳���
	std::thread t(func_,threadId_); //�����̶߳����ͬʱ�������߳�
	// C++11��˵���̶߳���t ���̺߳���func_
	// ��������ֲ��������̶߳���������ˣ������̺߳������ڣ�������Ҫ���̶߳�������Ϊ�����߳�
	t.detach();

}

int Thread::getId() const {
	return threadId_;
}

////////////////// Task����ʵ��
Task::Task()
	:result_(nullptr)
{}

void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run()); // ���﷢����̬����
	}
}

void Task::setResult(Result* res) {
	result_ = res;
}

//Result������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isvalid)
	:task_(task)
	,isValid_(isvalid)
{
	task->setResult(this);
}

Any Result::get() {  // �û����õ�
	if (!isValid_) {
		return "";
	}
	sem_.wait(); //task�������û��ִ���꣬����������û����߳�
	//�������ִ�����ˣ�sem_��postһ�£��ź���������Դ��
	//����Ϳ��������ź�����һ����Դ�����ش���ֵ���õ�any_
	return std::move(any_);
}

void Result::setVal(Any any) { 
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post(); //�Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}

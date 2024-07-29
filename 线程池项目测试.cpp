// 线程池项目.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "threadpool.h"
#include<thread>
using namespace std;

/* 有些场景，是希望能够获取线程执行任务的返回值的
举例：
1+...+30000的和
thread1 1 + ... + 10000
thread2 10001 + ... + 20000
...
main thread:给每一个线程分配计算区间，并等待它们算完返回结果，合并最终的结果
*/
using uLong = unsigned long long;
class MyTask :public Task {
public:
    MyTask(int begin, int end) :begin_(begin), end_(end) {}
    // 问题1：怎么涉及run函数的返回值，可以表示任意类型呢？
    Any run() { // run方法最终就在线程池分配多线程中去执行了
        std::cout << "begin threadFunc" <<std::this_thread::get_id()<< std::endl;
        std::cout << "end threadFunc" << std::this_thread::get_id() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++) {
            sum += i;
        }
        return sum;
    }
private:
    int begin_;
    int end_;
};


int main()
{   
    {
        ThreadPool pool;
        // 开始启动线程池 
        pool.start(4);

        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res4 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res5 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        //std::cout << sum1 << endl;

        uLong sum1 = res1.get().cast_<uLong>();
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();
        std::cout << "main over!" << endl;
    }
    return 0;



#if 0
    // 问题： ThreadPool对象析构以后，怎么样把线程池相关的线程资源全部回收?
    {
        ThreadPool pool;

        //问题2：如何设计这里的Result机制呢？
        pool.setMode(PoolMode::MODE_CACHED);

        // 开始启动线程池
        pool.start(4);

        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        pool.submitTask(std::make_shared<MyTask>(1, 100000000));

        // 随着task被执行完，task对象没了，依赖于task对象的Result对象也没了
        uLong sum1 = res1.get().cast_<uLong>();
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();

        cout << (sum1 + sum2 + sum3) << endl;
    }
    

    //pool.submitTask(std::make_shared<MyTask>());
    //pool.submitTask(std::make_shared<MyTask>());
    //pool.submitTask(std::make_shared<MyTask>());
    //pool.submitTask(std::make_shared<MyTask>());

    //std::this_thread::sleep_for(std::chrono::seconds(5));
    getchar();
    return 0;

#endif
}

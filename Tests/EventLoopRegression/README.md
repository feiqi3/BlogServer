# EventLoop / FTimeQueue 回归测试

仅测试现有基础设施，不依赖新增的 JobGraph、HTTP 客户端、第三方库、生产配置或网络服务。

```bash
cmake -S Tests/EventLoopRegression -B build/eventloop-regression
cmake --build build/eventloop-regression --parallel 2
ctest --test-dir build/eventloop-regression --output-on-failure
```

Windows 可明确使用 `-G "MinGW Makefiles"` 并指定配套 GCC、G++ 和 mingw32-make；Linux 可直接使用上述命令。

覆盖范围：

- 在非零内存上默认构造 FTimeQueue，验证定时器 ID 从 1 开始递增、取消可重复、任务只执行一次。
- Loop 正常退出和 ForceQuit + Quit 路径清理当前线程的 Loop 指针；正常关闭回调仍可获取所属 Loop。
- 嵌套尝试运行另一个 Loop 时，不误清理外层 Loop 的线程归属；退出销毁后同线程仍可运行新的 Loop。
- 四个线程投递 400 个任务，任务全部且只在目标 Loop 线程执行一次；后台 Loop 退出不污染提交线程。

ForceQuit 的既有语义保留：它跳过回调，终止循环仍使用 Quit。本修复没有改变停止语义、任务队列或监听器实现。

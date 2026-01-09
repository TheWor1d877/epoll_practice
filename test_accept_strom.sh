#!/bin/bash
# 测试ET模式accept处理连接风暴的能力

echo "=== 测试1：连接风暴（ET vs LT accept性能） ==="
echo ""

# 同时发起10000个快速连接
echo "模拟10000个客户端同时连接..."
start_time=$(date +%s%N)

# 同时创建10000个连接，每个发一条消息
for i in {1..10000}; do
     nc -z localhost 8080 &
done

# 等待所有连接处理完成
wait
end_time=$(date +%s%N)
connect_time=$(( (end_time - start_time) / 1000000 ))

echo "10000个连接处理时间：${connect_time}ms"
echo ""

# 清理
kill $SERVER_PID 2>/dev/null
sleep 1

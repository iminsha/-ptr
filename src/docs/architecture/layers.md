# 技术落地实现框架（分层）

- BSP 层：GPIO / Timer / UART / I2C(模拟) / 1-Wire(时序) 等基础
- Driver 层：DS1302、DS18B20、24C02、LCD1602、IR、Stepper、Buzzer
- Kernel 层：1ms tick、软定时器、事件队列、调度循环（非阻塞）
- Service 层：规则引擎、日程管理、配置管理、日志系统、错误管理
- App 层：UI 状态机（页面/编辑/确认）、动作编排（scene）

强制要求：任何耗时操作必须设计成可分片/可让出，不能靠 delay 堵塞主循环。

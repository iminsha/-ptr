# 代码目录建议（可直接照此建工程）

/Project
  /bsp
    bsp_gpio.c/.h
    bsp_timer.c/.h
    bsp_uart.c/.h
    bsp_i2c_soft.c/.h
    bsp_onewire.c/.h
  /drivers
    ds1302.c/.h
    ds18b20.c/.h
    at24c02.c/.h
    lcd1602.c/.h
    ir_nec.c/.h
    stepper_uln2003.c/.h
    buzzer.c/.h
  /kernel
    tick.c/.h            // 1ms tick
    soft_timer.c/.h
    event_queue.c/.h
    scheduler.c/.h       // 事件循环
  /services
    cfg_store.c/.h       // 配置读写 + 校验
    rule_engine.c/.h
    schedule.c/.h
    logger.c/.h
    error_mgr.c/.h
    device_model.c/.h    // 统一设备/动作抽象
  /app
    ui_pages.c/.h        // LCD页面与编辑状态机
    app_main.c/.h        // 系统启动/初始化/主流程
    actions.c/.h         // 动作执行入口
  main.c
  README.md
